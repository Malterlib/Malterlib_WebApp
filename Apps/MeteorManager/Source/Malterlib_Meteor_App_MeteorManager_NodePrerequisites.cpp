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

		g_Dispatch(*mp_FileActors) > [ProgramDirectory, NodeDirectory, ThisActor = fg_ThisActor(this), NodeUser = mp_NodeUser]() mutable -> TCContinuation<CNodeInfo>
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
					fsp_SetupUser(NodeUser);
					
					auto Files = CFile::fs_FindFiles(ProgramDirectory + "/node-*");
					
					if (!Files.f_IsEmpty())
					{
						DistFile = Files[0];

						CStr TmpDirectory = NodeDirectory + "/.tmp";
						CFile::fs_CreateDirectory(NodeDirectory);

						if (CFile::fs_FileExists(TmpDirectory))
							CFile::fs_DeleteDirectoryRecursive(TmpDirectory);

						CFile::fs_CreateDirectory(TmpDirectory);

						CStr NodeCertificateDirectory = NodeDirectory + "/certificates";
						
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

						CFile::fs_SetOwnerAndGroupRecursive(NodeDirectory, NodeUser.m_Name, NodeUser.m_Name);

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

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Package(CStr const &_BundleName, CMeteorManagerOptions::EPackageType _Type)
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		TCContinuation<void> Continuation;

		g_Dispatch(*mp_FileActors) >
			[
				_BundleName
				, _Type
				, ProgramDirectory
				, ThisActor = fg_ThisActor(this)
				, NodeUserName = mp_NodeUser.m_Name
				, bForceAppsReinstall = mp_bForceAppsReinstall
			]
			() mutable -> TCContinuation<void>
			{
				TCContinuation<void> Continuation;
				
				try
				{
					DMibLogCategoryStr(_BundleName);
					DLog(Info, "Setting up bundle");

					CStr BundleDirectory = ProgramDirectory + "/" + _BundleName;
					CStr MeteorBundleFileName = ProgramDirectory + "/" + _BundleName + ".tar.gz";
					CStr NewChecksum = fsp_GetFileChecksum(MeteorBundleFileName).f_GetString();
					CStr MeteorBundleChecksumFileName = ProgramDirectory + "/" + _BundleName + ".tar.gz.installed.md5";
					bool bDoInstall = false;

					if (bForceAppsReinstall)
						bDoInstall = true;
					else if (CFile::fs_FileExists(BundleDirectory))
					{
						CStr OldChecksum;

						if (CFile::fs_FileExists(MeteorBundleChecksumFileName))
							OldChecksum = CFile::fs_ReadStringFromFile(MeteorBundleChecksumFileName, true);

						if (NewChecksum != OldChecksum)
						{
							DLog(Info, "New bundle detected with checksum '{}' that differs from installed checksum '{}'. Installing new bundle", NewChecksum, OldChecksum);
							bDoInstall = true;
						}
						else
							DLog(Info, "Installed bundle with checksum '{}' is up to date", NewChecksum, OldChecksum);
					}
					else
					{
						DLog(Info, "No bundle installed, installing bundle with checksum '{}'", NewChecksum);
						bDoInstall = true;
					}
					
					TCContinuation<void> InstallContinuation;

					if (bDoInstall)
					{
						if (CFile::fs_FileExists(MeteorBundleChecksumFileName))
							CFile::fs_DeleteFile(MeteorBundleChecksumFileName); // Make sure to retry the next time if failure below
						if (CFile::fs_FileExists(BundleDirectory))
							CFile::fs_DeleteDirectoryRecursive(BundleDirectory);

						ThisActor(&CMeteorManagerActor::f_ExtractTar, MeteorBundleFileName, ProgramDirectory) > InstallContinuation / [=]
							{
								TCActorResultVector<CStr> Results;
								try
								{
									if (_Type == CMeteorManagerOptions::EPackageType_Meteor)
									{
										auto Files = CFile::fs_FindFiles(BundleDirectory + "/programs/web.browser/*.css");
										Files.f_Insert(CFile::fs_FindFiles(BundleDirectory+ "/programs/web.browser/*.js"));
										for (auto &File : Files)
										{
											ThisActor
												(
													&CMeteorManagerActor::f_LaunchTool
													, "gzip"
													, BundleDirectory
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
										
										if (!CFile::fs_FileExists(BundleDirectory + "/.installed"))
										{
											CFile::fs_SetOwnerAndGroupRecursive(BundleDirectory, NodeUserName, NodeUserName);

											ThisActor
												(
													&CMeteorManagerActor::f_LaunchTool
													, ProgramDirectory + "/node_dist/bin/npm"
													, BundleDirectory + "/programs/server"
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

										// Make bundle directory read only for node process
										DMibLogCategoryStr(_BundleName);
										DLog
											(
												Info
												, "Setting owner on bundle directory: {} ({}) - {} ({})"
												, NSys::fg_UserManagement_GetProcessRealUserName()
												, NSys::fg_UserManagement_GetProcessRealUser()
												, NSys::fg_UserManagement_GetProcessRealGroupName()
												, NSys::fg_UserManagement_GetProcessRealGroup()
											)
										;
										try
										{
											CFile::fs_SetOwnerAndGroupRecursive(BundleDirectory, NSys::fg_UserManagement_GetProcessRealUserName(), NSys::fg_UserManagement_GetProcessRealGroupName());
											CFile::fs_WriteStringToFile(MeteorBundleChecksumFileName, NewChecksum, false);
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
					
					InstallContinuation > Continuation / [_BundleName, Continuation]
						{
							DMibLogCategoryStr(_BundleName);
							DLog(Info, "Setting up bundle was successful");
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
			> Continuation / [this, Continuation]()
			{
				Continuation.f_SetResult();
			}
		;

		return Continuation;
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
