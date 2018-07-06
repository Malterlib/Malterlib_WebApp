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
		f_LaunchTool(CProcessLaunch::fs_GetBashPath(), ProgramDirectory, {SetupOSFile}, "OSSetup", ELogVerbosity_Errors, Environment) > Continuation.f_ReceiveAny();
		return Continuation;
	}
	
	CHashDigest_MD5 CMeteorManagerActor::fsp_GetFileChecksum(CStr const &_File)
	{
		CStr ChecksumFileName = _File + ".md5";
		if (CFile::fs_FileExists(ChecksumFileName))
			return CHashDigest_MD5::fs_FromString(CFile::fs_ReadStringFromFile(ChecksumFileName).f_Left(32));
		return CFile::fs_GetFileChecksum(_File);
	}

	void CMeteorManagerActor::fsp_SetupPrerequisites_ServerUser
		(
			CUser &_User
#ifdef DPlatformFamily_Windows
			, CStrSecure &o_Password
#endif
			, CStr const &_Directory
			, CStr const &_SSLDirectory
		)
	{
#ifdef DPlatformFamily_Windows
		fsp_SetupUser(_User, o_Password);
#else
		fsp_SetupUser(_User);
#endif

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

		CFile::fs_SetOwnerAndGroupRecursive(_Directory, _User.m_UserName, _User.m_GroupName);
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_NodeExtract()
	{
		if (!mp_bNeedNode)
			return fg_Explicit();

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr NodeDirectory = fp_GetDataPath("node");
		
		struct CNodeInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
			bool m_bForceAppReinstall = false;
		};

		bool bNeedNode = false;

		for (auto &Package : mp_Options.m_Packages)
		{
			if
				(
				 	Package.m_Type != CMeteorManagerOptions::EPackageType_FastCGI
				 	&& Package.m_Type != CMeteorManagerOptions::EPackageType_Websocket
				 	&& !Package.f_IsNpmStatic()
				)
			{
				bNeedNode = true;
			}
		}

		TCContinuation<void> Continuation;
		g_Dispatch(*mp_FileActors)
			> [ProgramDirectory, NodeDirectory, ThisActor = fg_ThisActor(this), NodeUser = mp_NodeUser, MongoSSLDirectory = fp_GetMongoSSLDirectory(), bNeedNode]
			() mutable -> TCContinuation<CNodeInfo>
			{
				DLog(Info, "Extracting node distribution");
				
				TCContinuation<CNodeInfo> Continuation;
				
				CStr DistFile;

				bool bDoInstall = false;
				CStr DistDirectory = ProgramDirectory + "/node_dist";
				CStr ChecksumFileName = ProgramDirectory + "/node.installed.md5";
				CStr NewChecksum;
				CNodeInfo NodeInfo;
				NodeInfo.m_User = NodeUser;

				try
				{
#ifdef DPlatformFamily_Windows
					fsp_SetupPrerequisites_ServerUser(NodeInfo.m_User, NodeInfo.m_UserPassword, NodeDirectory, MongoSSLDirectory);
#else
					fsp_SetupPrerequisites_ServerUser(NodeInfo.m_User, NodeDirectory, MongoSSLDirectory);
#endif
					
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

								NodeInfo.m_bForceAppReinstall = true;
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
						if (bNeedNode)
							DLog(Error, "No node distribution found");
						
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
							
							Continuation.f_SetResult(NodeInfo);
						}
					;
				}
				else
				{
					Continuation.f_SetResult(NodeInfo);
					return Continuation;
				}
				
				return Continuation;
			}
			> Continuation / [this, Continuation](CNodeInfo const &_NodeInfo)
			{
				mp_NodeUser = _NodeInfo.m_User;
				mp_bForceAppsReinstall = _NodeInfo.m_bForceAppReinstall;
#ifdef DPlatformFamily_Windows
				if (!_NodeInfo.m_UserPassword.f_IsEmpty())
				{
					mp_AppState.m_StateDatabase.m_Data["Users"][_NodeInfo.m_User.m_UserName]["Password"] = _NodeInfo.m_UserPassword;
					mp_AppState.f_SaveStateDatabase() > Continuation;
					return;
				}
#endif
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
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
			bool m_bPasswordChanged = false;
#endif
		};
		
		auto &PackageOptions = fg_Const(mp_Options.m_Packages)[_PackageName];

		auto PackageUser = CUser
			{
				mp_pUniqueUserGroup->f_GetUser("mib_pkg_{}_{}"_f << mp_Options.m_ManagerName << _PackageName)
				, mp_pUniqueUserGroup->f_GetGroup("mib_pkg_{}_{}"_f << mp_Options.m_ManagerName << _PackageName)
			}
		;
		auto DefaultUser = (_Type == CMeteorManagerOptions::EPackageType_FastCGI) ? mp_FastCGIUser : (_Type == CMeteorManagerOptions::EPackageType_Websocket) ? mp_WebsocketUser : mp_NodeUser;
		bool bSeparateUser = PackageOptions.m_bSeparateUser;

		g_Dispatch(*mp_FileActors) >
			[
				_PackageName
				, _Type
				, ProgramDirectory
				, ThisActor = fg_ThisActor(this)
			 	, DefaultUser
				, PackageUser
#ifdef DPlatformFamily_Windows
			 	, PackagePassword = fp_GetUserPassword(PackageUser.m_UserName)
			 	, DefaultPassword = fp_GetUserPassword(DefaultUser.m_UserName)
#endif
				, PackageHomeDirectory = fg_Format("{}/Home_{}", ProgramDirectory, _PackageName)
				, MongoSSLDirectory = fp_GetMongoSSLDirectory()
				, bSeparateUser
				, bOwnPackageDirectory = PackageOptions.m_bOwnPackageDirectory
			 	, bIsStatic = PackageOptions.f_IsNpmStatic()
				, ExcludeGzipPatterns = PackageOptions.m_ExcludeGzipPatterns
				, bForceAppsReinstall = mp_bForceAppsReinstall
			]
			() mutable -> TCContinuation<CPackageInfo>
			{
				TCContinuation<CPackageInfo> Continuation;
				
				try
				{
					DMibLogCategoryStr(_PackageName);
					DLog(Info, "Setting up package");

					CPackageInfo PackageInfo;
					PackageInfo.m_User = DefaultUser;
#ifdef DPlatformFamily_Windows
					PackageInfo.m_UserPassword = DefaultPassword;
#endif

					auto User = DefaultUser;
					CStr UserHomePath = ProgramDirectory
						/
						(
						 	_Type == CMeteorManagerOptions::EPackageType_FastCGI
						 	? "FastCGIHome"
						 	: (_Type == CMeteorManagerOptions::EPackageType_Websocket ? "WebsocketHome" : "node")
						)
					;

					if (bSeparateUser)
					{
						PackageInfo.m_User = PackageUser;
#ifdef DPlatformFamily_Windows
						PackageInfo.m_UserPassword = PackagePassword;
						fsp_SetupPrerequisites_ServerUser(PackageInfo.m_User, PackageInfo.m_UserPassword, PackageHomeDirectory, MongoSSLDirectory);
						PackageInfo.m_bPasswordChanged = PackageInfo.m_UserPassword != PackagePassword;
#else
						fsp_SetupPrerequisites_ServerUser(PackageInfo.m_User, PackageHomeDirectory, MongoSSLDirectory);
#endif
						User = PackageInfo.m_User;
						UserHomePath = PackageHomeDirectory;
					}
					
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
						if (!bOwnPackageDirectory && CFile::fs_FileExists(PackageDirectory))
							CFile::fs_DeleteDirectoryRecursive(PackageDirectory, true);

						ThisActor(&CMeteorManagerActor::f_ExtractTar, MeteorPackageFileName, ProgramDirectory) > InstallContinuation / [=]
							{
								TCActorResultVector<CStr> Results;
								try
								{
									if (_Type == CMeteorManagerOptions::EPackageType_Meteor || _Type == CMeteorManagerOptions::EPackageType_FastCGI || bIsStatic)
									{
										CStr StaticRoot;
										if (bIsStatic)
											StaticRoot = PackageDirectory;
										else if (_Type == CMeteorManagerOptions::EPackageType_FastCGI)
											StaticRoot = PackageDirectory + "/static";
										else
											StaticRoot = PackageDirectory + "/programs/web.browser";

										TCVector<CStr> Files;
										if (bIsStatic)
										{
											for (auto &File : CFile::fs_FindFiles(StaticRoot + "/*", EFileAttrib_File, true))
											{
												CStr RelativePath = CFile::fs_MakePathRelative(File, StaticRoot);
												bool bExcluded = false;
												for (auto &Pattern : ExcludeGzipPatterns)
												{
													if (NStr::fg_StrMatchWildcard(RelativePath.f_GetStr(), Pattern.f_GetStr()) == EMatchWildcardResult_WholeStringMatchedAndPatternExhausted)
													{
														bExcluded = true;
														break;
													}
												}
												if (bExcluded)
													continue;
												Files.f_Insert(File);
											}
										}
										else if (_Type == CMeteorManagerOptions::EPackageType_Meteor)
										{
											Files.f_Insert(CFile::fs_FindFiles(StaticRoot + "/*.css", EFileAttrib_File));
											Files.f_Insert(CFile::fs_FindFiles(StaticRoot + "/*.js", EFileAttrib_File));
										}
										else
										{
											Files.f_Insert(CFile::fs_FindFiles(StaticRoot + "/*.css", EFileAttrib_File, true));
											Files.f_Insert(CFile::fs_FindFiles(StaticRoot + "/*.js", EFileAttrib_File, true));
										}

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
													, fg_Default()
#ifdef DPlatformFamily_Windows
													, fg_Default()
#endif
												)
												> Results.f_AddResult()
											;
										}
										
										if (!CFile::fs_FileExists(PackageDirectory + "/.installed"))
										{
											CFile::fs_SetOwnerAndGroupRecursive(PackageDirectory, User.m_UserName, User.m_GroupName);

											if (_Type == CMeteorManagerOptions::EPackageType_Meteor)
											{
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
														, UserHomePath
														, User.m_UserName
														, User.m_GroupName
#ifdef DPlatformFamily_Windows
														, PackageInfo.m_UserPassword
#endif
													)
													> Results.f_AddResult()
												;
											}
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
											CStr UserName;
											if (bOwnPackageDirectory)
												CFile::fs_SetOwnerAndGroupRecursive(PackageDirectory, User.m_UserName, User.m_GroupName);
											else
											{
												CFile::fs_SetOwnerAndGroupRecursive
													(
													 	PackageDirectory
													 	, NSys::fg_UserManagement_GetProcessRealUserName()
													 	, NSys::fg_UserManagement_GetProcessRealGroupName()
													)
												;
											}

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
					
					InstallContinuation > Continuation / [_PackageName, Continuation, PackageInfo]
						{
							DMibLogCategoryStr(_PackageName);
							DLog(Info, "Setting up package was successful");
							Continuation.f_SetResult(PackageInfo);
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
#ifdef DPlatformFamily_Windows
				if (_PackageInfo.m_bPasswordChanged && !_PackageInfo.m_UserPassword.f_IsEmpty())
				{
					mp_AppState.m_StateDatabase.m_Data["Users"][_PackageInfo.m_User.m_UserName]["Password"] = _PackageInfo.m_UserPassword;
					mp_AppState.f_SaveStateDatabase() > Continuation;
					return;
				}
#endif
				
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
	
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Servers()
	{
		TCContinuation<void> Continuation;
		fp_SetupPrerequisites_OSSetup()
			+ fp_SetupPrerequisites_NodeExtract()
			+ fp_SetupPrerequisites_FastCGI()
			+ fp_SetupPrerequisites_Websocket()
			> Continuation / [Continuation, this]
			{
				fp_SetupPrerequisites_Packages() > Continuation / [Continuation, this]
					{
						fp_SetupPrerequisites_UploadS3() > Continuation;
					}
				;
			}
		;
		return Continuation;
	}
}
