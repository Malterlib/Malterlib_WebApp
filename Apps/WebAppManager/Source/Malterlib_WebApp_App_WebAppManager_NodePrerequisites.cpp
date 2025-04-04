// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Compression/ZLib>

namespace NMib::NWebApp::NWebAppManager
{
	mint CWebAppManagerActor::fp_GetNumNodes() const
	{
		mint nNodes = 0;
		for (auto &Package : mp_Options.m_Packages)
			nNodes += Package.m_Concurrency;
		return nNodes;
	}

	mint CWebAppManagerActor::fp_NeedsLocalIPs() const
	{
		for (auto &Package : mp_Options.m_Packages)
		{
			if (Package.f_IsDynamicServer() && !Package.m_bUnixSocket)
				return true;
		}
		return false;
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_OSSetup()
	{
#ifdef DPlatformFamily_Windows
		co_return {};
#else
		if (!fp_NeedsLocalIPs())
			co_return {};

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr SetupOSFile = ProgramDirectory + "/Source/Malterlib_WebApp_App_WebAppManager_OSSetup.sh";

		TCMap<CStr, CStr> Environment;
		Environment["NumNodeServers"] = "{}"_f << fp_GetNumNodes();
		Environment["PlatformFamily"] = DMibStringize(DPlatformFamily);
		Environment["LoopbackPrefix"] = "{}"_f << mp_LoopbackPrefix;

		co_await f_LaunchTool(CProcessLaunch::fs_GetBashPath(), ProgramDirectory, {SetupOSFile}, "OSSetup", ELogVerbosity_Errors, Environment);
		co_return {};
#endif
	}

	CHashDigest_MD5 CWebAppManagerActor::fsp_GetFileChecksum(CStr const &_File)
	{
		CStr ChecksumFileName = _File + ".md5";
		if (CFile::fs_FileExists(ChecksumFileName))
			return CHashDigest_MD5::fs_FromString(CFile::fs_ReadStringFromFile(ChecksumFileName).f_Left(32));
		return CFile::fs_GetFileChecksum(_File);
	}

	void CWebAppManagerActor::fsp_SetupPrerequisites_ServerUser
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
					, {}
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

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_NodeExtract()
	{
		if (!mp_bNeedNode)
			co_return {};

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
					Package.m_Type != CWebAppManagerOptions::EPackageType_FastCGI
					&& Package.m_Type != CWebAppManagerOptions::EPackageType_Websocket
					&& !Package.f_IsStatic()
				)
			{
				bNeedNode = true;
			}
		}

		CNodeInfo NodeInfo;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			NodeInfo = co_await
				(
					g_Dispatch(BlockingActorCheckout)
					/ [ProgramDirectory, NodeDirectory, ThisActor = fg_ThisActor(this), NodeUser = mp_NodeUser, MongoSSLDirectory = fp_GetMongoSSLDirectory(), bNeedNode]
					() mutable -> TCFuture<CNodeInfo>
					{
						DLog(Info, "Extracting node distribution");

						CStr DistFile;

						bool bDoInstall = false;
						CStr DistDirectory = ProgramDirectory + "/node_dist";
						CStr ChecksumFileName = ProgramDirectory + "/node.installed.md5";
						CStr NewChecksum;
						CNodeInfo NodeInfo;
						NodeInfo.m_User = NodeUser;

						auto CaptureExceptions = co_await g_CaptureExceptions;

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

							co_return fg_Move(NodeInfo);
						}

						if (!bDoInstall)
							co_return fg_Move(NodeInfo);

						co_await ThisActor(&CWebAppManagerActor::f_ExtractTar, DistFile, ProgramDirectory);

						CFile::fs_RenameFile(ProgramDirectory + "/" + CFile::fs_GetFileNoExt(CFile::fs_GetFileNoExt(DistFile)), DistDirectory);

						CFile::fs_SetOwnerAndGroupRecursive
							(
								DistDirectory
								, NSys::fg_UserManagement_GetProcessRealUserName()
								, NSys::fg_UserManagement_GetProcessRealGroupName()
							)
						;
						CFile::fs_WriteStringToFile(ChecksumFileName, NewChecksum, false);

						co_return fg_Move(NodeInfo);
					}
				)
			;
		}

		mp_NodeUser = NodeInfo.m_User;
		mp_bForceAppsReinstall = NodeInfo.m_bForceAppReinstall;
#ifdef DPlatformFamily_Windows
		if (!NodeInfo.m_UserPassword.f_IsEmpty())
			co_await fp_SaveUserPassword(NodeInfo.m_User.m_UserName, NodeInfo.m_UserPassword);
#endif
		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Packages()
	{
		TCFutureVector<void> Results;

		for (auto &Package : mp_Options.m_Packages)
			fp_SetupPrerequisites_Package(Package.f_GetName(), Package.m_Type) > Results;

		co_await fg_AllDone(Results);

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Package(CStr _PackageName, CWebAppManagerOptions::EPackageType _Type)
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		struct CPackageInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
			bool m_bPasswordChanged = false;
#endif
			CStr m_MainFile;
		};

		auto &PackageOptions = fg_Const(mp_Options.m_Packages)[_PackageName];

		auto PackageUser = CUser
			{
				mp_pUniqueUserGroup->f_GetUser("{}pkg_{}_{}"_f << mp_Options.m_UserNamePrefix << mp_Options.m_ManagerName << _PackageName)
				, mp_pUniqueUserGroup->f_GetGroup("{}pkg_{}_{}"_f << mp_Options.m_UserNamePrefix << mp_Options.m_ManagerName << _PackageName)
			}
		;
		auto DefaultUser = (_Type == CWebAppManagerOptions::EPackageType_FastCGI) 
			? mp_FastCGIUser
			: (_Type == CWebAppManagerOptions::EPackageType_Websocket) ? mp_WebsocketUser : mp_NodeUser
		;
		bool bSeparateUser = PackageOptions.m_bSeparateUser;

		CPackageInfo PackageInfo;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			PackageInfo = co_await
				(
					g_Dispatch(BlockingActorCheckout) /
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
						, bIsStatic = PackageOptions.f_IsStatic()
						, ExcludeGzipPatterns = PackageOptions.m_ExcludeGzipPatterns
						, bForceAppsReinstall = mp_bForceAppsReinstall
						, NpmExecutable = fp_GetNodeExecutable("npm", PackageOptions.m_bUseSystemNode)
						, bUnixSocket = PackageOptions.m_bUnixSocket
						, SocketRootDirectory = PackageOptions.m_bUnixSocket && PackageOptions.m_Concurrency ? fp_GetPackageSocketRoot(_PackageName) : CStr()
						, NginxUserGroup = mp_NginxUser.m_GroupName
						, Concurrency = PackageOptions.m_Concurrency
					]
					() mutable -> TCFuture<CPackageInfo>
					{
						auto CaptureExceptions = co_await g_CaptureExceptions;

						DLogWithCategoryStr(_PackageName, Info, "Setting up package");

						CPackageInfo PackageInfo;
						PackageInfo.m_User = DefaultUser;
	#ifdef DPlatformFamily_Windows
						PackageInfo.m_UserPassword = DefaultPassword;
	#endif

						auto User = DefaultUser;
						CStr UserHomePath = ProgramDirectory
							/
							(
								_Type == CWebAppManagerOptions::EPackageType_FastCGI
								? "FastCGIHome"
								: (_Type == CWebAppManagerOptions::EPackageType_Websocket ? "WebsocketHome" : "node")
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

						if (bUnixSocket && Concurrency)
						{
							CFile::fs_CreateDirectory(SocketRootDirectory);
							CFile::fs_SetOwnerAndGroupRecursive
								(
									SocketRootDirectory
									, User.m_UserName
									, NginxUserGroup
								)
							;
							CFile::fs_SetUnixAttributesRecursive
								(
									SocketRootDirectory
									, EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_GroupRead | EFileAttrib_GroupWrite
									, EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute | EFileAttrib_GroupRead | EFileAttrib_GroupWrite | EFileAttrib_GroupExecute
								)
							;
						}

						CStr PackageDirectory = ProgramDirectory + "/" + _PackageName;
						CStr PackageFileName = ProgramDirectory + "/" + _PackageName + ".tar.gz";
						CStr NewChecksum = fsp_GetFileChecksum(PackageFileName).f_GetString();
						CStr PackageChecksumFileName = ProgramDirectory + "/" + _PackageName + ".tar.gz.installed.md5";
						bool bDoInstall = false;

						if (bForceAppsReinstall)
							bDoInstall = true;
						else if (CFile::fs_FileExists(PackageDirectory))
						{
							CStr OldChecksum;

							if (CFile::fs_FileExists(PackageChecksumFileName))
								OldChecksum = CFile::fs_ReadStringFromFile(PackageChecksumFileName, true);

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

						if (bDoInstall)
						{
							if (CFile::fs_FileExists(PackageChecksumFileName))
								CFile::fs_DeleteFile(PackageChecksumFileName); // Make sure to retry the next time if failure below
							if (!bOwnPackageDirectory && CFile::fs_FileExists(PackageDirectory))
								CFile::fs_DeleteDirectoryRecursive(PackageDirectory, true);

							co_await ThisActor(&CWebAppManagerActor::f_ExtractTar, PackageFileName, ProgramDirectory);

							TCVector<CBlockingActorCheckout> BlockingActorCheckouts;
							auto fGetCheckout = [&, iCheckout = mint(0), nMaxCheckouts = NSys::fg_Thread_GetVirtualCores()]() mutable -> CBlockingActorCheckout &
								{
									if (BlockingActorCheckouts.f_GetLen() < nMaxCheckouts)
									{
										++iCheckout;
										return BlockingActorCheckouts.f_Insert(fg_BlockingActor());
									}
									iCheckout = iCheckout % nMaxCheckouts;
									auto &Return = BlockingActorCheckouts[iCheckout];
									++iCheckout;

									return Return;
								}
							;

							TCFutureVector<CStr> Results;
							CStr StaticRoot;
							if (bIsStatic)
								StaticRoot = PackageDirectory;
							else if (_Type == CWebAppManagerOptions::EPackageType_Meteor)
								StaticRoot = PackageDirectory + "/programs/web.browser";
							else
								StaticRoot = PackageDirectory + "/static";

							TCVector<CStr> Files;
							if (bIsStatic)
							{
								for (auto &File : CFile::fs_FindFiles(StaticRoot + "/*", EFileAttrib_File, true))
								{
									CStr RelativePath = CFile::fs_MakePathRelative(File, StaticRoot);
									bool bExcluded = false;
									for (auto &Pattern : ExcludeGzipPatterns)
									{
										if
										(
											NStr::fg_StrMatchWildcard(RelativePath.f_GetStr(), Pattern.f_GetStr())
											== EMatchWildcardResult_WholeStringMatchedAndPatternExhausted
										)
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
							else if (_Type == CWebAppManagerOptions::EPackageType_Meteor)
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
								g_Dispatch(fGetCheckout()) / [File]() -> CStr
									{
										NCompression::fg_CompressGZip(File, File + ".gz");
										return "";
									}
									> Results
								;
							}

							if (!CFile::fs_FileExists(PackageDirectory + "/.installed"))
							{
								if (_Type == CWebAppManagerOptions::EPackageType_Meteor)
								{
									CFile::fs_SetOwnerAndGroupRecursive(PackageDirectory, User.m_UserName, User.m_GroupName);
									ThisActor
										(
											&CWebAppManagerActor::f_LaunchTool
											, NpmExecutable
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
										> Results
									;
								}
							}

							co_await fg_AllDone(Results);
							BlockingActorCheckouts.f_Clear();

							// Make package directory read only for node process
							DLogWithCategoryStr
								(
									_PackageName
									, Info
									, "Setting owner on package directory: {} ({}) - {} ({})"
									, NSys::fg_UserManagement_GetProcessRealUserName()
									, NSys::fg_UserManagement_GetProcessRealUser()
									, NSys::fg_UserManagement_GetProcessRealGroupName()
									, NSys::fg_UserManagement_GetProcessRealGroup()
								)
							;
							CStr UserName;
							if (bOwnPackageDirectory)
							{
								for (auto &File : CFile::fs_FindFiles(PackageDirectory / "*", EFileAttrib_File, true))
									CFile::fs_MakeFileWritable(File);
								CFile::fs_SetOwnerAndGroupRecursive(PackageDirectory, User.m_UserName, User.m_GroupName);
							}
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

							CFile::fs_WriteStringToFile(PackageChecksumFileName, NewChecksum, false);
						}

						CStr PackageFile = PackageDirectory / "package.json";
						if (CFile::fs_FileExists(PackageFile))
						{
							try
							{
								auto PackageJSON = CJSONSorted::fs_FromString(CFile::fs_ReadStringFromFile(PackageFile, true), PackageFile);
								if (auto pValue = PackageJSON.f_GetMember("main", EJSONType_String))
									PackageInfo.m_MainFile = pValue->f_String();
							}
							catch (NException::CException const &_Exception)
							{
								DLogWithCategoryStr(_PackageName, Error, "Failed to parse package.json: {}", _Exception);
							}
						}

						DLogWithCategoryStr(_PackageName, Info, "Setting up package was successful");
						co_return fg_Move(PackageInfo);
					}
				)
			;
		}

		auto &Package = mp_Options.m_Packages[_PackageName];
		Package.m_User = PackageInfo.m_User;
		Package.m_MainFile = PackageInfo.m_MainFile;

#ifdef DPlatformFamily_Windows
		if (PackageInfo.m_bPasswordChanged && !PackageInfo.m_UserPassword.f_IsEmpty())
			co_await fp_SaveUserPassword(PackageInfo.m_User.m_UserName, PackageInfo.m_UserPassword);
#endif

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Customization()
	{
		if (!mp_pCustomization)
		co_return {};

		TCMap<CStr, CUser> Users;

		Users("node", mp_NodeUser);
		Users("fcgi", mp_FastCGIUser);
		Users("websocket", mp_WebsocketUser);
		Users("nginx", mp_NginxUser);

		for (auto &Package : mp_Options.m_Packages)
		{
			if (!Package.m_bSeparateUser)
			continue;
			Users(("package_{}"_f << mp_Options.m_Packages.fs_GetKey(Package)).f_GetStr(), Package.m_User);
		}

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout) / [pCustomization = mp_pCustomization, Tags = mp_Tags, Users]
				{
					pCustomization->f_SetupPrerequisites(Tags, Users);
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Servers()
	{
		co_await fp_SetupPrerequisites_Mongo(); // This is needed first because we need the mongo certificates

		co_await
			(
				fp_SetupPrerequisites_OSSetup()
				+ fp_SetupPrerequisites_NodeExtract()
				+ fp_SetupPrerequisites_FastCGI()
				+ fp_SetupPrerequisites_Websocket()
			)
		;

		co_await fp_SetupPrerequisites_Packages();
		co_await fp_SetupPrerequisites_UploadS3FileChangeNotifications();
		co_await fp_SetupPrerequisites_UploadS3();

		co_return {};
	}
}
