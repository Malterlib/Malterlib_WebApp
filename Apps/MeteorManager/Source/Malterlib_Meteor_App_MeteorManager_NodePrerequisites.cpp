// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMeteor::NMeteorManager
{
	mint CMeteorManagerActor::fp_GetNumNodes() const
	{
		mint nNodes = 0;
		for (auto &Package : mp_Options.m_Packages)
			nNodes += Package.m_Concurrency;
		return nNodes;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_OSSetup()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr SetupOSFile = ProgramDirectory + "/Source/Malterlib_Meteor_App_MeteorManager_OSSetup.sh";

		TCMap<CStr, CStr> Environment;
		Environment["NumNodeServers"] = fg_Format("{}", fp_GetNumNodes());
		Environment["PlatformFamily"] = DMibStringize(DPlatformFamily);
		Environment["LoopbackPrefix"] = fg_Format("{}", mp_Options.m_LoopbackPrefix);

		TCContinuation<void> Continuation;
		f_LaunchTool(SetupOSFile, ProgramDirectory, {}, "OSSetup", ELogVerbosity_Errors, Environment) > Continuation.f_ReceiveAny();
		return Continuation;
	}
	
	CHashDigest_MD5 CMeteorManagerActor::fsp_GetFileChecksum(CStr const &_File)
	{
		CStr ChecksumFileName = _File + ".md5";
		if (CFile::fs_FileExists(ChecksumFileName))
			return CHashDigest_MD5::fs_FromString(CFile::fs_ReadStringFromFile(ChecksumFileName).f_Left(32));
		return CFile::fs_GetFileChecksum(_File);
	}

	void CMeteorManagerActor::fsp_SetupPrerequisites_NodeUser(CUser &_User, CStr const &_Directory, CStr const &_SSLDirectory)
	{
		fsp_SetupUser(_User);

		CStr TmpDirectory = _Directory + "/.tmp";
		CFile::fs_CreateDirectory(_Directory);

		if (CFile::fs_FileExists(TmpDirectory))
			CFile::fs_DeleteDirectoryRecursive(TmpDirectory);

		CFile::fs_CreateDirectory(TmpDirectory);

		CStr NodeCertificateDirectory = _Directory + "/certificates";
		
		if (!_SSLDirectory.f_IsEmpty() && CFile::fs_FileExists(_SSLDirectory))
		{
			CFile::fs_DiffCopyFileOrDirectory
				(
					_SSLDirectory
					, NodeCertificateDirectory
					, [](CFile::EDiffCopyChange _Change, NStr::CStr const &_Source, NStr::CStr const &_Destination, NStr::CStr const &_Link) -> CFile::EDiffCopyChangeAction
					{
						if (_Change == CFile::EDiffCopyChange_FileDeleted)
							return CFile::EDiffCopyChangeAction_Skip;
						if (_Change == CFile::EDiffCopyChange_LinkDeleted)
							return CFile::EDiffCopyChangeAction_Skip;
						if (_Change == CFile::EDiffCopyChange_DirectoryDeleted)
							return CFile::EDiffCopyChangeAction_Skip;
						
						return CFile::EDiffCopyChangeAction_Perform;
					}
					, 0.0f
				)
			;
		}
		
		if (CFile::fs_FileExists(NodeCertificateDirectory))
		{
			CFile::fs_SetUnixAttributesRecursive
				(
					NodeCertificateDirectory
					, NFile::EFileAttrib_UserRead, NFile::EFileAttrib_UserRead | NFile::EFileAttrib_UserExecute
					, false
				)
			;
		}

		CFile::fs_SetOwnerAndGroupRecursive(_Directory, _User.m_Name, _User.m_Name);
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_NodeExtract()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr NodeDirectory = fp_GetDataPath("node");
		
		struct CNodeInfo
		{
			CUser m_User = {""};
			bool m_bForceAppReinstall = false;
		};
	
		TCContinuation<void> Continuation;
		g_Dispatch(*mp_FileActors)
			> [ProgramDirectory, NodeDirectory, ThisActor = fg_ThisActor(this), NodeUser = mp_NodeUser, MongoSSLDirectory = fp_GetMongoSSLDirectory()]
			() mutable -> TCContinuation<CNodeInfo>
			{
				DLog(Info, "Extracting node distribution");
				
				TCContinuation<CNodeInfo> Continuation;
				
				CStr DistFile;

				bool bDoInstall = false;
				bool bForceAppReinstall = false;
				CStr DistDirectory = ProgramDirectory + "/node_dist";
				CStr ChecksumFileName = ProgramDirectory + "/node.installed.md5";
				CStr NewChecksum;

				try
				{
					fsp_SetupPrerequisites_NodeUser(NodeUser, NodeDirectory, MongoSSLDirectory);
					
					auto Files = CFile::fs_FindFiles(ProgramDirectory + "/node-*.tar.gz");
					
					if (!Files.f_IsEmpty())
					{
						DistFile = Files[0];
						NewChecksum = fsp_GetFileChecksum(DistFile).f_GetString();
						
						if (CFile::fs_FileExists(DistDirectory))
						{
							CStr OldChecksum;

							if (CFile::fs_FileExists(ChecksumFileName))
								OldChecksum = CFile::fs_ReadStringFromFile(ChecksumFileName, true);

							if (NewChecksum != OldChecksum)
							{
								DLog
									(
										Info
										, "New node distribution detected with checksum '{}' that differs from installed checksum '{}'. Installing new distribution"
										, NewChecksum
										, OldChecksum
									)
								;
								CFile::fs_DeleteDirectoryRecursive(DistDirectory);

								bForceAppReinstall = true;
								bDoInstall = true;
							}
							else
								DLog(Info, "Installed node distribution with checksum '{}' is up to date", NewChecksum);
						}
						else
						{
							DLog(Info, "No node distribution installed, installing distribution with checksum '{}'", NewChecksum);
							if (CFile::fs_FileExists(ChecksumFileName))
								CFile::fs_DeleteFile(ChecksumFileName); // Make sure of retry if failure
							bDoInstall = true;
						}
					}
					else
					{
						DLog(Error, "No node distribution found");
						
						CNodeInfo NodeInfo;
						NodeInfo.m_User = NodeUser;
						NodeInfo.m_bForceAppReinstall = bForceAppReinstall;

						Continuation.f_SetResult(NodeInfo);
						return Continuation;
					}
				}
				catch (NException::CException const &)
				{
					Continuation.f_SetCurrentException();
					return Continuation;
				}
				
				if (bDoInstall)
				{
					ThisActor(&CMeteorManagerActor::f_ExtractTar, DistFile, ProgramDirectory) > Continuation / [=]
						{
							try
							{
								CFile::fs_RenameFile(ProgramDirectory + "/" + CFile::fs_GetFileNoExt(CFile::fs_GetFileNoExt(DistFile)), DistDirectory);

								CFile::fs_SetOwnerAndGroupRecursive
									(
										DistDirectory
										, NSys::fg_UserManagement_GetProcessRealUserName()
										, NSys::fg_UserManagement_GetProcessRealGroupName()
									)
								;
								CFile::fs_WriteStringToFile(ChecksumFileName, NewChecksum, false);
							}
							catch (NException::CException const &)
							{
								Continuation.f_SetCurrentException();
								return;
							}
							
							CNodeInfo NodeInfo;
							NodeInfo.m_User = NodeUser;
							NodeInfo.m_bForceAppReinstall = bForceAppReinstall;

							Continuation.f_SetResult(NodeInfo);
						}
					;
				}
				else
				{
					CNodeInfo NodeInfo;
					NodeInfo.m_User = NodeUser;
					NodeInfo.m_bForceAppReinstall = bForceAppReinstall;

					Continuation.f_SetResult(NodeInfo);
					return Continuation;
				}
				
				return Continuation;
			}
			> Continuation / [this, Continuation](CNodeInfo const &_NodeInfo)
			{
				mp_NodeUser = _NodeInfo.m_User;
				mp_bForceAppsReinstall = _NodeInfo.m_bForceAppReinstall;
				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Packages()
	{
		TCActorResultVector<void> Results;
		
		for (auto &Package : mp_Options.m_Packages)
			fp_SetupPrerequisites_Package(Package.f_GetName(), Package.m_Type) > Results.f_AddResult();
	
		TCContinuation<void> Continuation;
		Results.f_GetResults() > Continuation / [Continuation]
			{
				Continuation.f_SetResult();
			}
		;
		
		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Package(CStr const &_PackageName, CMeteorManagerOptions::EPackageType _Type)
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		TCContinuation<void> Continuation;

		struct CPackageInfo
		{
			CUser m_User = {""};
		};
		
		auto &PackageOptions = fg_Const(mp_Options.m_Packages)[_PackageName];

		g_Dispatch(*mp_FileActors) >
			[
				_PackageName
				, _Type
				, ProgramDirectory
				, ThisActor = fg_ThisActor(this)
				, NodeUserName = mp_NodeUser.m_Name
				, User = CUser{fg_Format("mib_node_{}_{}", mp_Options.m_ManagerName, _PackageName)}
				, HomeDirectory = fg_Format("{}/node_{}", ProgramDirectory, _PackageName)
				, MongoSSLDirectory = fp_GetMongoSSLDirectory()
				, bSeparateUser = PackageOptions.m_bSeparateUser
				, bForceAppsReinstall = mp_bForceAppsReinstall
			]
			() mutable -> TCContinuation<CPackageInfo>
			{
				TCContinuation<CPackageInfo> Continuation;
				
				try
				{
					DMibLogCategoryStr(_PackageName);
					DLog(Info, "Setting up package");

					if (bSeparateUser)
						fsp_SetupPrerequisites_NodeUser(User, HomeDirectory, MongoSSLDirectory);
					
					CStr PackageDirectory = ProgramDirectory + "/" + _PackageName;
					CStr MeteorPackageFileName = ProgramDirectory + "/" + _PackageName + ".tar.gz";
					CStr NewChecksum = fsp_GetFileChecksum(MeteorPackageFileName).f_GetString();
					CStr MeteorPackageChecksumFileName = ProgramDirectory + "/" + _PackageName + ".tar.gz.installed.md5";
					bool bDoInstall = false;

					if (bForceAppsReinstall)
						bDoInstall = true;
					else if (CFile::fs_FileExists(PackageDirectory))
					{
						CStr OldChecksum;

						if (CFile::fs_FileExists(MeteorPackageChecksumFileName))
							OldChecksum = CFile::fs_ReadStringFromFile(MeteorPackageChecksumFileName, true);

						if (NewChecksum != OldChecksum)
						{
							DLog(Info, "New package detected with checksum '{}' that differs from installed checksum '{}'. Installing new package", NewChecksum, OldChecksum);
							bDoInstall = true;
						}
						else
							DLog(Info, "Installed package with checksum '{}' is up to date", NewChecksum, OldChecksum);
					}
					else
					{
						DLog(Info, "No package installed, installing package with checksum '{}'", NewChecksum);
						bDoInstall = true;
					}
					
					TCContinuation<void> InstallContinuation;

					if (bDoInstall)
					{
						if (CFile::fs_FileExists(MeteorPackageChecksumFileName))
							CFile::fs_DeleteFile(MeteorPackageChecksumFileName); // Make sure to retry the next time if failure below
						if (CFile::fs_FileExists(PackageDirectory))
							CFile::fs_DeleteDirectoryRecursive(PackageDirectory);

						ThisActor(&CMeteorManagerActor::f_ExtractTar, MeteorPackageFileName, ProgramDirectory) > InstallContinuation / [=]
							{
								TCActorResultVector<CStr> Results;
								try
								{
									if (_Type == CMeteorManagerOptions::EPackageType_Meteor)
									{
										auto Files = CFile::fs_FindFiles(PackageDirectory + "/programs/web.browser/*.css");
										Files.f_Insert(CFile::fs_FindFiles(PackageDirectory+ "/programs/web.browser/*.js"));
										for (auto &File : Files)
										{
											ThisActor
												(
													&CMeteorManagerActor::f_LaunchTool
													, "gzip"
													, PackageDirectory
													, fg_CreateVector<CStr>("-k", "-9", File)
													, CStr{"GZipStatic"}
													, ELogVerbosity_Errors
													, fg_Default()
													, true
													, fg_Default()
													, fg_Default()
												)
												> Results.f_AddResult()
											;
										}
										
										if (!CFile::fs_FileExists(PackageDirectory + "/.installed"))
										{
											CFile::fs_SetOwnerAndGroupRecursive(PackageDirectory, NodeUserName, NodeUserName);

											ThisActor
												(
													&CMeteorManagerActor::f_LaunchTool
													, ProgramDirectory + "/node_dist/bin/npm"
													, PackageDirectory + "/programs/server"
													, fg_CreateVector<CStr>("install", "--silent")
													, CStr{"GZipStatic"}
													, ELogVerbosity_Errors
													, fg_Default()
													, true
													, ProgramDirectory + "/node"
													, NodeUserName
												)
												> Results.f_AddResult()
											;
										}
									}
								}
								catch (NException::CException const &)
								{
									InstallContinuation.f_SetCurrentException();
									return;
								}

								
								Results.f_GetResults() > [=](TCAsyncResult<TCVector<TCAsyncResult<CStr>>> &&_Results)
									{
										if (!fg_CombineResults(InstallContinuation, fg_Move(_Results)))
											return;

										// Make package directory read only for node process
										DMibLogCategoryStr(_PackageName);
										DLog
											(
												Info
												, "Setting owner on package directory: {} ({}) - {} ({})"
												, NSys::fg_UserManagement_GetProcessRealUserName()
												, NSys::fg_UserManagement_GetProcessRealUser()
												, NSys::fg_UserManagement_GetProcessRealGroupName()
												, NSys::fg_UserManagement_GetProcessRealGroup()
											)
										;
										try
										{
											CFile::fs_SetOwnerAndGroupRecursive(PackageDirectory, NSys::fg_UserManagement_GetProcessRealUserName(), NSys::fg_UserManagement_GetProcessRealGroupName());
											CFile::fs_WriteStringToFile(MeteorPackageChecksumFileName, NewChecksum, false);
										}
										catch (NException::CException const &)
										{
											InstallContinuation.f_SetCurrentException();
											return;
										}
									}
								;
							}
						;
					}
					else
						InstallContinuation.f_SetResult();
					
					InstallContinuation > Continuation / [_PackageName, Continuation, User]
						{
							DMibLogCategoryStr(_PackageName);
							DLog(Info, "Setting up package was successful");
							CPackageInfo PackageInfo;
							PackageInfo.m_User = User;
							Continuation.f_SetResult();
						}
					;
				}
				catch (NException::CException const &)
				{
					Continuation.f_SetCurrentException();
					return Continuation;
				}
				
				return Continuation;
			}
			> Continuation / [this, Continuation, _PackageName](CPackageInfo const &_PackageInfo)
			{
				auto &Package = mp_Options.m_Packages[_PackageName];
				Package.m_User = _PackageInfo.m_User;
				
				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Customization()
	{
		if (!mp_pCustomization)
			return fg_Explicit();
		
		return g_Dispatch(*mp_FileActors) > [pCustomization = mp_pCustomization, Tags = mp_Tags]
			{
				pCustomization->f_SetupPrerequisites(Tags);
			}
		;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Node()
	{
		TCContinuation<void> Continuation;
		fp_SetupPrerequisites_OSSetup()
			+ fp_SetupPrerequisites_NodeExtract()
			> Continuation / [Continuation, this]
			{
				fp_SetupPrerequisites_Packages() > Continuation;
			}
		;
		return Continuation;
	}
}
