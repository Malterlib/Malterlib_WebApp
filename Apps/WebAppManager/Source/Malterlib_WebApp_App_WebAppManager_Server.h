// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#ifdef __cplusplus

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Concurrency/ActorSequencer>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProcessLaunch>
#include <Mib/File/ChangeNotificationActor>
#include <Mib/Mongo/Client>
#include <Mib/Storage/Optional>
#include <Mib/Security/UniqueUserGroup>
#include <Mib/Web/AWS/S3>
#include <Mib/Web/AWS/CloudFront>
#include <Mib/Web/AWS/Lambda>
#include <Mib/Web/Curl>
#include <Mib/WebApp/WebCertificateDeploy>
#include <Mib/Cloud/NetworkTunnelsServer>

#include "Malterlib_WebApp_App_WebAppManager_Helpers.h"

namespace NMib::NWebApp::NWebAppManager
{
	struct CWebAppManagerOptions
	{
		enum EPackageType
		{
			EPackageType_Meteor
			, EPackageType_Npm
			, EPackageType_Custom
			, EPackageType_FastCGI
			, EPackageType_Websocket
			, EPackageType_Static
		};

		struct CEnvironmentVariable
		{
			CStr const &f_GetName() const
			{
				return TCMap<CStr, CEnvironmentVariable>::fs_GetKey(*this);
			}

			CStr m_Setting;
			TCOptional<CStr> m_Default;
			TCSet<TCSet<CStr>> m_RequiredTags;
			TCSet<CStr> m_ForbiddenTags;
		};

		struct CPackage
		{
			CStr const &f_GetName() const
			{
				return TCMap<CStr, CPackage>::fs_GetKey(*this);
			}

			bool f_IsStatic() const
			{
				return m_Type == EPackageType_Static
					||
					(
						m_Type == EPackageType_Npm
						&&
						(
							m_NpmBuildType == "Compile"
							|| m_NpmBuildType == "Build"
						)
					)
				;
			}

			bool f_IsNpmDynamic() const
			{
				return
					m_Type == EPackageType_Npm
					&&
					(
						m_NpmBuildType == "Start"
					)
				;
			}

			bool f_IsDynamicServer() const
			{
				return m_Type == EPackageType_Meteor || m_Type == EPackageType_FastCGI || m_Type == EPackageType_Websocket || f_IsNpmDynamic();
			}

			bool f_HasCustomExecutable() const
			{
				return m_Type == EPackageType_Custom || m_Type == EPackageType_FastCGI || m_Type == EPackageType_Websocket;
			}

			bool f_IsServer() const
			{
				return f_IsDynamicServer() || f_IsStatic();
			}

			TCVector<CStr> m_StartupDependencies;
			CStr m_NpmBuildType;
			CStr m_CustomExecutable;
			TCVector<CStr> m_CustomParams;
			CUser m_User{"", ""};
			CStr m_DomainPrefix;
			CStr m_SubPath;
			CStr m_RedirectsFile;
			CStr m_StickyCookie;
			CStr m_StickyHeader;
			CStr m_StaticPath;
			CStr m_MainFile;
			CStr m_ExternalRoot;
			TCVector<CStr> m_ExcludeGzipPatterns;
			TCMap<CStr, int64> m_UploadS3Priority;
			fp64 m_MemoryPerNode = 1.5;
			mint m_Concurrency = 1;
			EPackageType m_Type = EPackageType_Meteor;
			bool m_bSeparateUser = false;
			bool m_bOwnPackageDirectory = false;
			bool m_bAllowRobots = true;
			bool m_bUploadS3 = false;
		};

		struct CMongo
		{
			void f_SetDefaults(CWebAppManagerOptions const &_Options);

			CStr m_Directory = "../MongoManager/mongo/bin";
			CStr m_Host = NProcess::NPlatform::fg_Process_GetHostName();
			int16 m_Port = 25017;
			CStr m_ToolsUser;
			CStr m_ToolsGroup;
			CStr m_SSLDirectory;
			CStr m_DatabaseSetupScript;
			CStr m_DefaultDatabase;
			CStr m_DefaultReplicaName = "DefaultReplica";
		};

		CWebAppManagerOptions(CStr const &_ManagerName, CStr const &_ManagerDescription);
		void f_ParseSettings(CStr const &_Settings, CStr const &_FileName);

		CStr m_ManagerName;
		CStr m_FullManagerName;
		CStr m_ManagerDescription;
		CStr m_UserNamePrefix;
		TCMap<CStr, CPackage> m_Packages;
		TCMap<CStr, CEnvironmentVariable> m_Environment;
		CMongo m_Mongo;
		CStr m_DefaultDomain;
		CStr m_HTTPRedirectReferrerCookie;
		CStr m_S3BucketPrefix;

		CStr m_ContentSecurity_ScriptSrc;
		CStr m_ContentSecurity_ImgSrc;
		CStr m_ContentSecurity_MediaSrc;
		CStr m_ContentSecurity_FontSrc;
		CStr m_ContentSecurity_StyleSrc;
		CStr m_ContentSecurity_FrameSrc;
		CStr m_ContentSecurity_ConnectSrc;
		CStr m_ContentSecurity_ObjectSrc;
		CStr m_ContentSecurity_ChildSrc;
		CStr m_ContentSecurity_FormAction;
		CStr m_ContentSecurity_ReportURI;

		CStr m_AccessControl_AllowMethods;
		CStr m_AccessControl_AllowHeaders;
		CStr m_AccessControl_AllowOrigin;
		CStr m_AccessControl_MaxAge;

		uint16 m_DefaultWebPort = 3000;
		uint16 m_DefaultWebSSLPort = 3443;
		uint8 m_LoopbackPrefix = 0;
		bool m_bUseInternalNode = false;
		bool m_bRedirectWWW = false;
		bool m_bSaveUserPasswords = false;
		bool m_bServeAllSubdomains = false;
		bool m_bStartNginx = true;
	};

	struct ICWebAppManagerCustomization
	{
		ICWebAppManagerCustomization();
		virtual ~ICWebAppManagerCustomization();
		virtual void f_SetupPrerequisites(TCSet<CStr> const &_Tags, TCMap<CStr, CUser> const &_Users);
		virtual void f_CalculateSettings
			(
				TCMap<CStr, CStr> &o_Settings
				, CJSON &o_MeteorSettings
				, CStr const &_PackageName
				, CDistributedAppState const &_AppState
				, CWebAppManagerOptions const &_Options
				, CWebAppManagerOptions::CPackage const &_PackageOptions
				, TCFunction<CEJSON (CStr const &_Name, CEJSON const &_Default)> const &_fGetConfigValue
			)
		;
		virtual void f_ManipulateNginxConfig
			(
				CStr &o_Config
				, CDistributedAppState const &_AppState
				, CWebAppManagerOptions const &_Options
				, TCFunction<CEJSON (CStr const &_Name, CEJSON const &_Default)> const &_fGetConfigValue
				, TCSet<CStr> const &_Tags
				, CStr const &_FastCGIFile
			 	, TCMap<CStr, CStr> const &_PackageIPs
			)
		;
	};

	TCSharedPointer<ICWebAppManagerCustomization> fg_CreateWebAppManagerCustomization();

	struct CWebAppManagerActor : public CActor
	{
	public:
		using CActorHolder = CDelegatedActorHolder;

		enum ELogVerbosity
		{
			ELogVerbosity_None
			, ELogVerbosity_Errors
			, ELogVerbosity_Messages
			, ELogVerbosity_All
		};

		CWebAppManagerActor(CDistributedAppState &_AppState, CWebAppManagerOptions const &_Options);
		~CWebAppManagerActor();
		TCFuture<void> f_Startup();
		TCFuture<void> f_PreStop();

		static void fs_SetupEnvironment(CProcessLaunchParams &_Params);

		static mint fs_GetNodeFileLimits();
		static mint fs_GetNginxWorkerFileLimits();
		static mint fs_GetNginxFileLimits(mint _nNodes);

		TCFuture<CStr> f_LaunchTool
			(
				CStr const &_Executable
				, CStr const &_WorkingDir
				, TCVector<CStr> const &_Params
				, CStr const &_LogCategory
 				, ELogVerbosity _LogVerbosity
				, TCMap<CStr, CStr> const &_Environment = {}
				, bool _bSeparateStdErr = true
				, CStr const &_Home = {}
				, CStr const &_User = {}
				, CStr const &_Group = {}
#ifdef DPlatformFamily_Windows
				, CStrSecure const &_UserPassword = {}
#endif
			)
		;
		TCFuture<CStr> f_ExtractTar(CStr const &_TarFile, CStr const &_DestinationDir);

	private:
		enum EHostnamePrefix
		{
			EHostnamePrefix_None
			, EHostnamePrefix_Static
			, EHostnamePrefix_StaticSource
		};

		struct CAppLaunchKey
		{
			CStr m_PackageName;
			mint m_iAppSequence = 0;

			bool operator < (CAppLaunchKey const &_Right) const
			{
				return fg_TupleReferences(m_PackageName, m_iAppSequence) < fg_TupleReferences(_Right.m_PackageName, _Right.m_iAppSequence);
			}
		};

		struct CAppLaunch
		{
			CAppLaunchKey const &f_GetKey() const
			{
				return TCMap<CAppLaunchKey, CAppLaunch>::fs_GetKey(*this);
			}

			TCActor<CProcessLaunchActor> m_Launch;
			CActorSubscription m_LaunchSubscription;
			CActorSubscription m_TunnelSubscription;
			CStr m_LogCategory;
			CStr m_BackendIdentifier;
			bool m_bInitialLaunched = false;
		};

		TCFuture<void> fp_Destroy() override;

		void fp_ParseConfig_DDPSelf();
		void fp_ParseConfig();

		CStr fp_GetDataPath(CStr const &_Path) const;
		CStr fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const;
		TCFuture<CStr> fp_RunToolForVersionCheck
			(
				CStr const &_Tool
				, TCVector<CStr> const &_Arguments
			)
		;
#ifdef DPlatformFamily_Windows
		CStrSecure fp_GetUserPassword(CStr const &_User);
		TCFuture<void> fp_SaveUserPassword(CStr const &_User, CStrSecure const &_Password);
#endif
		static void fsp_SetupUser
			(
				CUser &_User
#ifdef DPlatformFamily_Windows
				, CStrSecure &o_Password
#endif
			)
		;
		TCFuture<void> fp_ExtractExeFS() const;
		TCFuture<void> fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion);
		TCFuture<void> fp_CleanupOldProcesses();
		CStr fp_GetNodeExecutable(CStr const &_Executable);

		CEJSON fp_GetConfigValue(CStr const &_Name, CEJSON const &_Default) const;

		mint fp_GetNumNodes() const;

		static CHashDigest_MD5 fsp_GetFileChecksum(CStr const &_File);
		static void fsp_SetupPrerequisites_ServerUser
			(
				CUser &_User
#ifdef DPlatformFamily_Windows
				, CStrSecure &o_Password
#endif
				, CStr const &_Directory
				, CStr const &_SSLDirectory
			)
		;

		TCFuture<void> fp_SetupPrerequisites_Servers();
		TCFuture<void> fp_SetupPrerequisites_FastCGI();
		TCFuture<void> fp_SetupPrerequisites_Websocket();
		TCFuture<void> fp_SetupPrerequisites_Nginx();
		TCFuture<void> fp_SetupPrerequisites_Customization();
		TCFuture<void> fp_SetupPrerequisites_NodeExtract();
		TCFuture<void> fp_SetupPrerequisites_Mongo();
		TCFuture<void> fp_SetupPrerequisites_Packages();
		TCFuture<void> fp_SetupPrerequisites_UploadS3();
		TCFuture<void> fp_SetupPrerequisites_UploadS3Perform();
		TCFuture<void> fp_SetupPrerequisites_UploadS3FileChangeNotifications();
		TCFuture<void> fp_SetupPrerequisites_UpdateAWSLambda(CAwsCredentials const &_AWSCredentials);
		TCFuture<void> fp_SetupPrerequisites_Package(CStr const &_PackageName, CWebAppManagerOptions::EPackageType _Type);
		TCFuture<void> fp_SetupPrerequisites_OSSetup();

		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		CStr fp_GetMongoSSLDirectory() const;
		TCFuture<void> fp_RunMongoScript(CStr const &_Script, CStr const &_Database, fp32 _Timeout);

		TCFuture<void> fp_SetupMongo();

		static CStr fsp_GetVersionString();
		TCFuture<void> fp_UpdateVersionHistory();

		CStr fp_GetPackageHostname(CStr const &_PackageName, EHostnamePrefix _Prefix) const;
		CStr fp_GetPackageLocalURL(CStr const &_PackageName) const;
		CStr fp_GetRootURL(CStr const &_Hostname, CStr const &_SubPath) const;
		CStr fp_GetAppIPAddress(CAppLaunch const &_AppLaunch) const;
		CStr fp_GetAppLocalURL(CAppLaunch const &_AppLaunch) const;
		void fp_UpdateAppLaunch(CExceptionPointer const &_pException);
		void fp_LaunchApp(CAppLaunch &_AppLaunch, bool _bInitialLaunch);
		void fp_SetupNodeArguments(TCVector<CStr> &o_Arguments, CAppLaunch const &_AppLaunch, CWebAppManagerOptions::CPackage const &_PackageOptions);
		void fp_HandleNodeDebuggerOutput(CAppLaunch &_AppLaunch, CStr const &_StdErr);
		void fp_PopulateNodeEnvironment
			(
				CSystemEnvironment &o_Environment
				, TCMap<CStr, CStr> const &_CalculatedSettings
				, CAppLaunch const &_AppLaunch
				, CWebAppManagerOptions::CPackage const &_PackageOptions
			)
		;

		void fp_CreateAppLaunches();

		TCFuture<void> fp_StartNginx();
		TCFuture<void> fp_StartApps();
		TCFuture<void> fp_DestroyApps();
		TCFuture<void> fp_SetupNetworkTunnels();

		static TCMap<CStr, TCVector<CStr>> fsp_GetContentTypes();
		static CStr fsp_GetContentTypeForExtension(CStr const &_Extension);

		TCSharedPointer<CUniqueUserGroup> mp_pUniqueUserGroup;

		CWebAppManagerOptions mp_Options;
		TCRoundRobinActors<CSeparateThreadActor> mp_FileActors{5};

		TCSharedPointer<ICWebAppManagerCustomization> mp_pCustomization;

		CDistributedAppState &mp_AppState;

		TCActor<CFileChangeNotificationActor> mp_FileChangeNotificationActor = fg_Construct();
		TCVector<CActorSubscription> mp_S3FileChangeNotificationSubscriptions;

		TCActorSequencer<void> mp_S3UploadSequencer;
		TCActorSequencer<void> mp_S3FileReadSequencer{8};
		TCActorSequencer<void> mp_S3PrioritySequencer;

		CUser mp_NodeUser;
		CUser mp_FastCGIUser;
		CUser mp_WebsocketUser;
		CUser mp_NginxUser;
		CVersion mp_Version_Node{0, 10, 33};
		TCVector<CStr> mp_VersionHistory;
		TCSet<CStr> mp_Tags;

		bool mp_bStopped = false;
		bool mp_bForceAppsReinstall = false;
		bool mp_bNeedNode = false;
		bool mp_bNeedFCGI = false;
		bool mp_bNeedWebsocket = false;
		bool mp_bInitialS3UploadDone = false;
		bool mp_bPendingS3Upload = false;

		TCLinkedList<CToolLaunch> mp_ToolLaunches;

		TCMap<CAppLaunchKey, CAppLaunch> mp_AppLaunches;
		TCMap<CStr, zmint> mp_OutstandingLaunches;
		mutable TCMap<CStr, zmint> mp_CurrentPackageLocalURL;
		TCMap<CStr, TCVector<CStr>> mp_PackageLocalURLs;
		mint mp_AppSequence = 0;
		TCPromise<void> mp_AppLaunchesPromise;

		CStr mp_InstanceId;

		TCActor<CProcessLaunchActor> mp_NginxLaunch;
		CActorSubscription mp_NginxLaunchSubscription;

		TCRoundRobinActors<CCurlActor> mp_CurlActors{2 + 32};
		TCRoundRobinActors<CAwsS3Actor> mp_S3Actors{32};

		TCActor<CAwsCloudFrontActor> mp_CloudFrontActor;
		TCActor<CAwsLambdaActor> mp_LambdaActor;

#ifdef DPlatformFamily_Windows
		TCMap<CStr, CStrSecure> mp_UserPasswords;
#endif
		TCActor<CNetworkTunnelsServer> mp_NetworkTunnelsServer;

		TCActor<CWebCertificateDeployActor> mp_CertificateDeployActor;
		CActorSubscription mp_CertificateDeploySubscription;

		// Precalculated config

		CStr mp_Domain;
		CStr mp_DDPSelf;

		CStr mp_MongoDirectory;
		CStr mp_MongoHost;
		uint16 mp_MongoPort;
		CStr mp_MongoToolsUser;
		CStr mp_MongoToolsGroup;
		CStr mp_MongoSSLDirectory;
		CStr mp_MongoDatabase;
		CStr mp_MongoReplicaName;
		CStr mp_MongoAdminUserName;

		uint64 mp_WebPort = 3000;
		uint64 mp_WebSSLPort = 3443;
		uint8 mp_LoopbackPrefix = 0;
		bool mp_bIsStaging = false;
		bool mp_bAllowRobots = true;
		bool mp_bStartNgnix = true;
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NWebApp::NWebAppManager;
#endif

#else
#include <stdlib.h>
#endif
