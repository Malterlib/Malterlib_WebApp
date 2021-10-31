// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NWebApp::NWebAppManager
{
	CWebAppManagerActor::CWebAppManagerActor(CDistributedAppState &_AppState, CWebAppManagerOptions const &_Options)
		: mp_AppState(_AppState)
		, mp_pUniqueUserGroup{fg_Construct("/M/App/{}"_f << _Options.m_FullManagerName.f_Replace("_", "-"), _AppState.m_RootDirectory)}
		, mp_NodeUser
		{
			mp_pUniqueUserGroup->f_GetUser("{}node_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
			, mp_pUniqueUserGroup->f_GetGroup("{}node_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
		}
		, mp_FastCGIUser
		{
			mp_pUniqueUserGroup->f_GetUser("{}fcgi_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
			, mp_pUniqueUserGroup->f_GetGroup("{}fcgi_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
		}
		, mp_WebsocketUser
		{
			mp_pUniqueUserGroup->f_GetUser("{}ws_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
			, mp_pUniqueUserGroup->f_GetGroup("{}ws_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
		}
		, mp_NginxUser
		{
			mp_pUniqueUserGroup->f_GetUser("{}nginx_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
			, mp_pUniqueUserGroup->f_GetGroup("{}nginx_{}"_f << _Options.m_UserNamePrefix << _Options.m_ManagerName)
		}
		, mp_Options(_Options)
		, mp_pCustomization(fg_CreateWebAppManagerCustomization())
		, mp_InstanceId(fg_RandomID())
	{
	}

	CWebAppManagerActor::~CWebAppManagerActor()
	{
	}

	TCFuture<void> CWebAppManagerActor::f_Startup()
	{
		DMibLogWithCategory
			(
				WebAppManager
				, Info
				, "Web App Manager ({}) starting, {} {}.{}{} {} {}"
				, mp_Options.m_ManagerName
				, DMalterlibBranch
				, DMibStringize(DProductVersionMajor)
				, DMibStringize(DProductVersionMinor)
				, DMibStringize(DProductVersionRevision)
				, DMibStringize(DPlatform)
				, DMibStringize(DConfig)
			)
		;

		mp_FileActors.f_Construct(fg_Construct(fg_Construct<CSeparateThreadActor>(), "File actor"));

		try
		{
			fp_ParseConfig();
		}
		catch (NException::CException const &_Exception)
		{
			co_return DMibErrorInstance(fg_Format("Failed to parse config: ", _Exception));
		}

		fp_CreateAppLaunches();

		DLog(Info, "Cleaning up old processes");
		co_await (fp_CleanupOldProcesses() % "Failed to clean up old processes");

		DLog(Info, "Done cleaning up, extracting ExeFS");
		co_await (fp_ExtractExeFS() % "Failed to extract ExeFS");

		DLog(Info, "Done extracting ExeFS, setting up node prerequisites and updating version history");
		co_await (fp_SetupPrerequisites_Servers() + fp_UpdateVersionHistory());

		DLog(Info, "Done setting up node prerequisites and updating version history, setting up customization prerequisites");
		co_await fp_SetupPrerequisites_Customization();

		DLog(Info, "Done setting up customization prerequisites, setting up nginx prerequisites");
		co_await fp_SetupPrerequisites_Nginx();

		DLog(Info, "Done setting up nginx prerequisites, checking node version");
		if (mp_bNeedNode)
			co_await fp_CheckVersion(fp_GetNodeExecutable("node"), "--version", "v{}.{}.{}", mp_Version_Node);

		DLog(Info, "Done checking node version, setting up mongo");
		co_await fp_SetupMongo();

		DLog(Info, "Done setting up mongo, starting apps");
		co_await (fp_StartApps() + fp_SetupNetworkTunnels());

		DLog(Info, "Done starting apps, starting nginx");
		co_await fp_StartNginx();

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::f_PreStop()
	{
		DLog(Debug, "Pre-stop server");
		mp_bStopped = true;

		TCActorResultVector<void> Destroys;
		for (auto &ToolLaunch : mp_ToolLaunches)
			ToolLaunch.m_ProcessLaunch.f_Destroy() > Destroys.f_AddResult();

		co_await Destroys.f_GetResults();
		co_await fp_DestroyApps().f_Wrap();

		if (mp_NginxLaunch)
			co_await mp_NginxLaunch.f_Destroy().f_Wrap();

		DLog(Debug, "Pre-stop server done");

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_Destroy()
	{
		DLog(Debug, "Destroy server");

		TCActorResultVector<void> Destroys;

		if (mp_CertificateDeployActor)
			fg_Move(mp_CertificateDeployActor).f_Destroy() > Destroys.f_AddResult();

		if (mp_CertificateDeploySubscription)
			fg_Exchange(mp_CertificateDeploySubscription, nullptr)->f_Destroy() > Destroys.f_AddResult();

		for (auto &ToolLaunch : mp_ToolLaunches)
			ToolLaunch.m_ProcessLaunch.f_Destroy() > Destroys.f_AddResult();

		mp_S3Actors.f_Destroy() > Destroys.f_AddResult();

		if (mp_CloudFrontActor)
			mp_CloudFrontActor.f_Destroy() > Destroys.f_AddResult();
		if (mp_LambdaActor)
			mp_LambdaActor.f_Destroy() > Destroys.f_AddResult();

		mp_CurlActors.f_Destroy()  > Destroys.f_AddResult();

		co_await Destroys.f_GetResults();

		co_await fp_DestroyApps().f_Wrap();
		DLog(Debug, "Destroy apps done");

		if (mp_AppLaunchHelper)
			co_await mp_AppLaunchHelper.f_Destroy();

		{
			TCActorResultVector<void> Destroys;
			for (auto &Launch : mp_AppLaunches)
			{
				if (Launch.m_TunnelSubscription)
				{
					Launch.m_TunnelSubscription->f_Destroy() > Destroys.f_AddResult();
					Launch.m_TunnelSubscription.f_Clear();
				}
			}

			if (mp_NetworkTunnelsServer)
				mp_NetworkTunnelsServer.f_Destroy() > Destroys.f_AddResult();
			mp_NetworkTunnelsServer.f_Clear();

			co_await Destroys.f_GetResults();
		}

		if (mp_NginxLaunch)
			co_await mp_NginxLaunch.f_Destroy();

		co_await mp_FileActors.f_Destroy();

		co_return {};
	}

#ifdef DPlatformFamily_Windows
	CStrSecure CWebAppManagerActor::fp_GetUserPassword(CStr const &_User)
	{
		if (mp_Options.m_bSaveUserPasswords)
		{
			if (auto pUsers = mp_AppState.m_StateDatabase.m_Data.f_GetMember("Users", EJSONType_Object))
			{
				if (auto pUser = pUsers->f_GetMember(_User, EJSONType_Object))
				{
					if (auto pPassword = pUser->f_GetMember("Password", EJSONType_String))
						return pPassword->f_String();
				}
			}
		}
		else
		{
			if (auto pPassword = mp_UserPasswords.f_FindEqual(_User))
				return *pPassword;
		}
		return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SaveUserPassword(CStr const &_User, CStrSecure const &_Password)
	{
		TCPromise<void> Promise;

		if (mp_Options.m_bSaveUserPasswords)
		{
			mp_AppState.m_StateDatabase.m_Data["Users"][_User]["Password"] = _Password;
			mp_AppState.f_SaveStateDatabase() > Promise;
		}
		else
		{
			mp_UserPasswords[_User] = _Password;
			Promise.f_SetResult();
		}
		return Promise.f_MoveFuture();
	}
#endif

	void CWebAppManagerActor::fsp_SetupUser
		(
			CUser &_User
#ifdef DPlatformFamily_Windows
			, CStrSecure &o_Password
#endif
		)
	{
		if (!NSys::fg_UserManagement_GroupExists(_User.m_GroupName, _User.m_GroupID))
			NSys::fg_UserManagement_CreateGroup(_User.m_GroupName, _User.m_GroupID);

		if (!NSys::fg_UserManagement_UserExists(_User.m_UserName, _User.m_UserID))
		{
#ifdef DPlatformFamily_Windows
			o_Password = fg_HighEntropyRandomID("23456789ABCDEFGHJKLMNPQRSTWXYZabcdefghijkmnopqrstuvwxyz&=*!@~^") + "2Dg&";
#endif
			NSys::fg_UserManagement_CreateUser
				(
					_User.m_GroupName
					, _User.m_UserName
#ifdef DPlatformFamily_Windows
					, o_Password
#else
					, ""
#endif
					, _User.m_UserName
					, "/dev/null"
					, _User.m_UserID
					, NSys::EUserManagementCreateUserFlag_None
				)
			;
		}
#ifdef DPlatformFamily_Windows
		else if (o_Password.f_IsEmpty())
		{
			o_Password = fg_HighEntropyRandomID("23456789ABCDEFGHJKLMNPQRSTWXYZabcdefghijkmnopqrstuvwxyz&=*!@~^") + "2Dg&";
			NSys::fg_UserManagement_SetUserPassword
				(
					_User.m_UserName
					, o_Password
				)
			;
		}
#endif
	}

	TCFuture<void> CWebAppManagerActor::fp_ExtractExeFS() const
	{
		TCPromise<void> Promise;

		return Promise <<= g_Dispatch(*mp_FileActors) / []
			{
				CExeFS ExeFS;
				if (!fg_OpenExeFS(ExeFS))
					DError("Failed to open ExeFS");

				CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

				CFileSystemInterface_VirtualFS MalterlibFS(ExeFS.m_FileSystem);
				CFileSystemInterface_Disk DiskFS;
				CTime SourceFileTime = MalterlibFS.f_GetWriteTime("");

				CTime DestinationFileTime;
				CStr ExeFSFileTimeFile = ProgramDirectory + "/ExeFS.time";

				if (CFile::fs_FileExists(ExeFSFileTimeFile))
				{
					TCBinaryStreamFile<> Stream;
					Stream.f_Open(ExeFSFileTimeFile, EFileOpen_Read | EFileOpen_ShareAll);
					Stream >> DestinationFileTime;
				}
				if (SourceFileTime == DestinationFileTime)
					return;

				auto Files = CFile::fs_FindFiles(ProgramDirectory + "/node-*");
				if (!Files.f_IsEmpty())
				{
					for (auto &File : Files)
						CFile::fs_DeleteDirectoryRecursive(File);
				}

				MalterlibFS.f_CopyFilesWithAttribs("*", DiskFS, ProgramDirectory);

				TCBinaryStreamFile<> Stream;
				Stream.f_Open(ExeFSFileTimeFile, EFileOpen_Write | EFileOpen_ShareAll);
				Stream << SourceFileTime;
			}
		;
	}

	TCFuture<void> CWebAppManagerActor::fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion)
	{
		TCPromise<void> Promise;

		fp_RunToolForVersionCheck(_Tool, fg_CreateVector<CStr>(_Argument)) > Promise % "Failed to check version" / [=](CStr &&_Data)
			{
				if (_Data.f_IsEmpty())
				{
					Promise.f_SetException(DErrorInstance(fg_Format("Failed get version with: {} {}", _Tool, _Argument)));
					return;
				}

				CVersion Version;
				aint nParsed = 0;
				(CStr::CParse(_ParseString) >> Version.m_Major >> Version.m_Minor >> Version.m_Revision).f_Parse(_Data, nParsed);

				if (nParsed != 3)
				{
					Promise.f_SetException(DErrorInstance(fg_Format("Failed to extract {} version from: {}", _Tool, _Data)));
					return;
				}

				if (Version < _NeededVersion)
				{
					Promise.f_SetException(DErrorInstance(fg_Format("{} version {} is less than the required version of {}", _Tool, Version, _NeededVersion)));
					return;
				}
				DLog(Info, "{} version {} found", _Tool, Version);
				Promise.f_SetResult();
			}
		;
		return Promise.f_MoveFuture();
	}

	CStr CWebAppManagerActor::fp_GetDataPath(CStr const &_Path) const
	{
		return CFile::fs_AppendPath(CFile::fs_GetProgramDirectory(), _Path);
	}

	CWebAppManagerOptions::CWebAppManagerOptions(CStr const &_ManagerName, CStr const &_ManagerDescription)
		: m_ManagerName(_ManagerName)
		, m_ManagerDescription(_ManagerDescription)
	{
	}

	void CWebAppManagerOptions::CMongo::f_SetDefaults(CWebAppManagerOptions const &_Options)
	{
		m_ToolsUser = _Options.m_UserNamePrefix + "mongo";
		m_ToolsGroup = _Options.m_UserNamePrefix + "mongo";
	}

	void CWebAppManagerOptions::f_ParseSettings(CStr const &_Settings, CStr const &_FileName)
	{
		CEJSON const Settings = CEJSON::fs_FromString(_Settings, _FileName);

		for (auto &PackageJSON : Settings["Packages"].f_Object())
		{
			auto &Package = m_Packages[PackageJSON.f_Name()];
			auto &PackageSettings = PackageJSON.f_Value().f_Object();

			auto &PackageType = PackageJSON.f_Value()["Type"].f_String();
			if (PackageType == "Meteor")
				Package.m_Type = EPackageType_Meteor;
			else if (PackageType == "FastCGI")
				Package.m_Type = EPackageType_FastCGI;
			else if (PackageType == "Websocket")
				Package.m_Type = EPackageType_Websocket;
			else if (PackageType == "Npm")
				Package.m_Type = EPackageType_Npm;
			else if (PackageType == "Custom")
				Package.m_Type = EPackageType_Custom;
			else if (PackageType == "Static")
				Package.m_Type = EPackageType_Static;
			else
				DMibError(fg_Format("Unknown package type: {}", PackageType));

			if (auto *pValue = PackageSettings.f_GetMember("MemoryPerNode"))
				Package.m_MemoryPerNode = pValue->f_AsFloat(1.5);

			if (auto *pValue = PackageSettings.f_GetMember("PortConcurrency", EJSONType_Integer))
				Package.m_PortConcurrency = pValue->f_Integer();

			if (auto *pValue = PackageSettings.f_GetMember("Concurrency", EJSONType_Integer))
				Package.m_Concurrency = pValue->f_Integer();
			else if (auto *pValue = PackageSettings.f_GetMember("Concurrency", EJSONType_String))
			{
				CStr Expression = pValue->f_String();
				Expression = Expression.f_Replace("{PhysicalMemoryGB}", fg_Format("{}", fp64(NProcess::NPlatform::fg_Process_GetPhysicalMemory()) / (1024.0*1024.0*1024.0)));
				Expression = Expression.f_Replace("{MemoryPerNode}", fg_Format("{}", Package.m_MemoryPerNode));

				Expression = fg_Format("{{xpr({})}", Expression);

				CStr EvaluatedExpression = fg_Format(Expression.f_GetStr(), 0.0);

				Package.m_Concurrency = fg_Min(fg_Max(EvaluatedExpression.f_ToFloat().f_ToInt(), 1), NSys::fg_Thread_GetVirtualCores());
			}

			if (auto *pValue = PackageSettings.f_GetMember("NpmBuildType"))
				Package.m_NpmBuildType = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("DomainPrefix"))
				Package.m_DomainPrefix = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("SubPath"))
				Package.m_SubPath = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("RedirectsFile"))
				Package.m_RedirectsFile = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("StickyCookie"))
				Package.m_StickyCookie = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("StickyHeader"))
				Package.m_StickyHeader = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("StartupDependencies"))
			{
				for (auto &Dependency : pValue->f_Array())
					Package.m_StartupDependencies.f_Insert(Dependency.f_String());
			}

			if (auto *pValue = PackageSettings.f_GetMember("CustomExecutable"))
				Package.m_CustomExecutable = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("CustomParams"))
			{
				for (auto &Dependency : pValue->f_Array())
					Package.m_CustomParams.f_Insert(Dependency.f_String());
			}

			if (auto *pValue = PackageSettings.f_GetMember("SeparateUser"))
				Package.m_bSeparateUser = pValue->f_Boolean();

			if (auto *pValue = PackageSettings.f_GetMember("OwnPackageDirectory"))
				Package.m_bOwnPackageDirectory = pValue->f_Boolean();

			if (auto *pValue = PackageSettings.f_GetMember("ExcludeGzipPatterns"))
			{
				for (auto &Dependency : pValue->f_Array())
					Package.m_ExcludeGzipPatterns.f_Insert(Dependency.f_String());
			}

			if (auto *pValue = PackageSettings.f_GetMember("RedirectsTemporary"))
			{
				for (auto &Redirect : pValue->f_Array())
					Package.m_RedirectsTemporary.f_Insert({Redirect["From"].f_String(), Redirect["To"].f_String()});
			}

			if (auto *pValue = PackageSettings.f_GetMember("RedirectsPermanent"))
			{
				for (auto &Redirect : pValue->f_Array())
					Package.m_RedirectsPermanent.f_Insert({Redirect["From"].f_String(), Redirect["To"].f_String()});
			}

			if (auto *pValue = PackageSettings.f_GetMember("AlternateSources"))
			{
				for (auto &Redirect : pValue->f_Array())
				{
					Package.m_AlternateSources.f_Insert
						(
							{
								Redirect["Pattern"].f_String()
								, Redirect["Destination"].f_String()
								, Redirect.f_GetMemberValue("Search", "").f_String()
								, Redirect.f_GetMemberValue("Replace", "").f_String()
							}
						)
					;
				}
			}

			if (auto *pValue = PackageSettings.f_GetMember("StaticPath"))
				Package.m_StaticPath = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("AllowRobots"))
				Package.m_bAllowRobots = pValue->f_Boolean();

			if (auto *pValue = PackageSettings.f_GetMember("UploadS3"))
				Package.m_bUploadS3 = pValue->f_Boolean();

			if (auto *pValue = PackageSettings.f_GetMember("MalterlibDistributedApp"))
				Package.m_bMalterlibDistributedApp = pValue->f_Boolean();

			if (auto *pValue = PackageSettings.f_GetMember("UploadS3Priority"))
			{
				for (auto &PriorityEntry : pValue->f_Object())
					Package.m_UploadS3Priority[PriorityEntry.f_Name()] = PriorityEntry.f_Value().f_Integer();
			}

			if (auto *pValue = PackageSettings.f_GetMember("UploadS3Prefix"))
				Package.m_UploadS3Prefix = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("ExternalRoot"))
				Package.m_ExternalRoot = pValue->f_String();
		}

		if (auto pValue = Settings.f_GetMember("AllowRobots"))
			m_bAllowRobots = pValue->f_Boolean();

		if (auto pValue = Settings.f_GetMember("AllowRobotsSitemap"))
			m_RobotsSitemap = pValue->f_String();

		m_RobotsAllow = Settings["AllowRobotsAllow"].f_StringArray();
		m_RobotsDisallow = Settings["AllowRobotsDisallow"].f_StringArray();

		m_FullManagerName = Settings["FullManagerName"].f_String();
		m_DefaultDomain = Settings["DefaultDomain"].f_String();
		m_UserNamePrefix = Settings["UserNamePrefix"].f_String();

		if (auto *pValue = Settings.f_GetMember("StartNginx"))
			m_bStartNginx = pValue->f_Boolean();

		if (auto *pValue = Settings.f_GetMember("DefaultWebPort"))
			m_DefaultWebPort = pValue->f_Integer();
		if (auto *pValue = Settings.f_GetMember("DefaultWebSSLPort"))
			m_DefaultWebSSLPort = pValue->f_Integer();

		if (auto *pValue = Settings.f_GetMember("HTTPRedirectReferrerCookie"))
			m_HTTPRedirectReferrerCookie = pValue->f_String();

		if (auto *pValue = Settings.f_GetMember("RedirectWWW"))
			m_bRedirectWWW = pValue->f_Boolean();

		if (auto *pValue = Settings.f_GetMember("ServeAllSubdomains"))
			m_bServeAllSubdomains = pValue->f_Boolean();

		if (auto *pValue = Settings.f_GetMember("S3BucketPrefix"))
			m_S3BucketPrefix = pValue->f_String();

		if (auto *pValue = Settings.f_GetMember("AllowRedirectsOutsideOfDomain"))
			m_bAllowRedirectsOutsideOfDomain = pValue->f_Boolean();

		if (auto *pValue = Settings.f_GetMember("AllowRedirectsOutsideOfDomainPatterns"))
			m_AllowRedirectsOutsideOfDomainPatterns = pValue->f_StringArray();

		if (auto *pContentSecurity = Settings.f_GetMember("ContentSecurity"))
		{
			if (auto *pValue = pContentSecurity->f_GetMember("DefaultSrc"))
				m_ContentSecurity_DefaultSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ScriptSrc"))
				m_ContentSecurity_ScriptSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ImgSrc"))
				m_ContentSecurity_ImgSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("MediaSrc"))
				m_ContentSecurity_MediaSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("FontSrc"))
				m_ContentSecurity_FontSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("StyleSrc"))
				m_ContentSecurity_StyleSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("FrameSrc"))
				m_ContentSecurity_FrameSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ConnectSrc"))
				m_ContentSecurity_ConnectSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ObjectSrc"))
				m_ContentSecurity_ObjectSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ChildSrc"))
				m_ContentSecurity_ChildSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ManifestSrc"))
				m_ContentSecurity_ManifestSrc = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("FormAction"))
				m_ContentSecurity_FormAction = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("ReportURI"))
				m_ContentSecurity_ReportURI = pValue->f_String();
		}

		if (auto *pContentSecurity = Settings.f_GetMember("AccessControl"))
		{
			if (auto *pValue = pContentSecurity->f_GetMember("AllowMethods"))
				m_AccessControl_AllowMethods = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("AllowHeaders"))
				m_AccessControl_AllowHeaders = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("AllowOrigin"))
				m_AccessControl_AllowOrigin = pValue->f_String();
			if (auto *pValue = pContentSecurity->f_GetMember("MaxAge"))
				m_AccessControl_MaxAge = pValue->f_String();
		}

		TCMap<CStr, CStr> Hostnames;
		for (auto &Package : m_Packages)
		{
			if (Package.f_IsServer())
				continue;
			CStr Hostname;
			if (Package.m_DomainPrefix.f_IsEmpty())
				Hostname = m_DefaultDomain;
			else
				Hostname = fg_Format("{}.{}", Package.m_DomainPrefix, m_DefaultDomain);

			if (Package.m_SubPath.f_IsEmpty())
				Hostname += "/{}"_f << Package.m_SubPath;

			auto &PackageName = *Hostnames(Hostname, Package.f_GetName());
			if (PackageName != Package.f_GetName())
			{
				DMibError
					(
						"Package '{0}' and '{1}' resolves to the same hostname '{2}'. Set DomainPrefix_{0}, DomainPrefix_{1}, SubPath_{0} or SubPath_{1}"_f
					 	<< PackageName
					 	<< Package.f_GetName()
					 	<< Hostname
					)
				;
			}
		}

		if (auto *pValue = Settings.f_GetMember("LoopbackPrefix"))
			m_LoopbackPrefix = pValue->f_Integer();

		if (auto *pValue = Settings.f_GetMember("SaveUserPasswords"))
			m_bSaveUserPasswords = pValue->f_Boolean();

		m_Mongo.f_SetDefaults(*this);

		auto &MongoJSON = Settings["Mongo"].f_Object();
		if (auto *pValue = MongoJSON.f_GetMember("Directory"))
			m_Mongo.m_Directory = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("Host"))
			m_Mongo.m_Host = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("Port"))
			m_Mongo.m_Port = pValue->f_Integer();
		if (auto *pValue = MongoJSON.f_GetMember("ToolsUser"))
			m_Mongo.m_ToolsUser = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("ToolsGroup"))
			m_Mongo.m_ToolsGroup = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("SSLDirectory"))
			m_Mongo.m_SSLDirectory = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("DatabaseSetupScript"))
			m_Mongo.m_DatabaseSetupScript = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("DefaultDatabase"))
			m_Mongo.m_DefaultDatabase = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("DefaultReplicaName"))
			m_Mongo.m_DefaultReplicaName = pValue->f_String();

		TCSet<TCSet<CStr>> DefaultReqiredTags;
		TCSet<CStr> DefaultForbiddenTags;

		if (auto *pValue = Settings.f_GetMember("EnvironmentDefaultTags"))
		{
			for (auto &TagJSON : pValue->f_Array())
			{
				auto Tag = TagJSON.f_String();
				if (Tag.f_StartsWith("!"))
					DefaultForbiddenTags[Tag.f_Extract(1)];
				else
				{
					TCSet<CStr> TagSet;
					while (!Tag.f_IsEmpty())
						TagSet[fg_GetStrSep(Tag, "|")];
					DefaultReqiredTags[TagSet];
				}
			}
		}
		if (auto *pValue = Settings.f_GetMember("Environment"))
		{
			for (auto &EnvVarJSON : fg_Const(pValue->f_Array()))
			{
				auto &EnvVar = m_Environment[EnvVarJSON["EnvVar"].f_String()];
				EnvVar.m_Setting = EnvVarJSON["ConfigVar"].f_String();
				if (auto *pValue = EnvVarJSON.f_GetMember("Default"))
					EnvVar.m_Default = pValue->f_String();

				EnvVar.m_RequiredTags = DefaultReqiredTags;
				EnvVar.m_ForbiddenTags = DefaultForbiddenTags;

				if (auto *pValue = EnvVarJSON.f_GetMember("Tags"))
				{
					for (auto &TagJSON : pValue->f_Array())
					{
						if (TagJSON.f_IsString())
						{
							CStr Tag = TagJSON.f_String();

							bool bAdd = true;
							if (Tag.f_StartsWith("-"))
							{
								bAdd = false;
								Tag = Tag.f_Extract(1);
							}

							bool bReqired = true;
							if (Tag.f_StartsWith("!"))
							{
								bReqired = false;
								Tag = Tag.f_Extract(1);
							}

							if (bReqired)
							{
								if (bAdd)
									EnvVar.m_RequiredTags[TCSet<CStr>{Tag}];
								else
									EnvVar.m_RequiredTags.f_Remove(TCSet<CStr>{Tag});
							}
							else
							{
								if (bAdd)
									EnvVar.m_ForbiddenTags[Tag];
								else
									EnvVar.m_ForbiddenTags.f_Remove(Tag);
							}
						}
						else
						{
							auto Tags = TagJSON.f_StringArray();
							TCSet<CStr> TagSet;
							bool bAdd = true;
							for (auto Tag : Tags)
							{
								if (Tag.f_StartsWith("-"))
								{
									bAdd = false;
									Tag = Tag.f_Extract(1);
								}

								if (Tag.f_StartsWith("!"))
								{
									DMibError
										(
											"You cannot specify forbidden tags `{}` in a tag set"_f
										 	<< Tag
										)
									;
								}

								TagSet[Tag];
							}

							if (bAdd)
								EnvVar.m_RequiredTags[TagSet];
							else
								EnvVar.m_RequiredTags.f_Remove(TagSet);
						}
					}
				}
			}
		}
	}

	ICWebAppManagerCustomization::ICWebAppManagerCustomization() = default;
	ICWebAppManagerCustomization::~ICWebAppManagerCustomization() = default;

	void ICWebAppManagerCustomization::f_CalculateSettings
		(
			TCMap<CStr, CStr> &o_Settings
			, CJSON &o_MeteorSettings
			, CStr const &_PackageName
			, CDistributedAppState const &_AppState
			, CWebAppManagerOptions const &_Options
			, CWebAppManagerOptions::CPackage const &_PackageOptions
			, TCFunction<CEJSON (CStr const &_Name, CEJSON const &_Default)> const &_fGetConfigValue
		)
	{
	}

	void ICWebAppManagerCustomization::f_ManipulateNginxConfig
		(
			CStr &o_Config
			, CDistributedAppState const &_AppState
			, CWebAppManagerOptions const &_Options
			, TCFunction<CEJSON (CStr const &_Name, CEJSON const &_Default)> const &_fGetConfigValue
			, TCSet<CStr> const &_Tags
			, CStr const &_FastCGIFile
		 	, TCMap<CStr, CStr> const &_PackageIPs
		)
	{
	}

	void ICWebAppManagerCustomization::f_SetupPrerequisites(TCSet<CStr> const &_Tags, TCMap<CStr, CUser> const &_Users)
	{
	}
}
