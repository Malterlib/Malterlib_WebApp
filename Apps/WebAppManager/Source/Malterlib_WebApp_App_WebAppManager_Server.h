// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#ifdef __cplusplus

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Concurrency/ActorSequencerActor>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProcessLaunch>
#include <Mib/File/ChangeNotificationActor>
#include <Mib/Mongo/Client>
#include <Mib/Mongo/MongoCertificateDeploy>
#include <Mib/Storage/Optional>
#include <Mib/Security/UniqueUserGroup>
#include <Mib/Web/AWS/S3>
#include <Mib/Web/AWS/CloudFront>
#include <Mib/Web/AWS/Lambda>
#include <Mib/Web/Curl>
#include <Mib/WebApp/WebCertificateDeploy>
#include <Mib/Cloud/NetworkTunnelsServer>
#include <Mib/Concurrency/DistributedAppLaunchHelper>

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
				return m_Concurrency && (m_Type == EPackageType_Meteor || m_Type == EPackageType_FastCGI || m_Type == EPackageType_Websocket || f_IsNpmDynamic());
			}

			bool f_HasCustomExecutable() const
			{
				return m_Type == EPackageType_Custom || m_Type == EPackageType_FastCGI || m_Type == EPackageType_Websocket;
			}

			bool f_IsServer() const
			{
				return f_IsDynamicServer() || f_IsStatic();
			}

			struct CRedirect
			{
				template <typename tf_CStream>
				void f_Stream(tf_CStream &_Stream)
				{
					_Stream % m_From;
					_Stream % m_To;
				}

				CStr m_From;
				CStr m_To;
			};

			struct CSearchReplace
			{
				template <typename tf_CStream>
				void f_Stream(tf_CStream &_Stream)
				{
					_Stream % m_Search;
					_Stream % m_Replace;
				}

				CStr m_Search;
				CStr m_Replace;
			};

			struct CAlternateSource
			{
				template <typename tf_CStream>
				void f_Stream(tf_CStream &_Stream)
				{
					_Stream % m_Pattern;
					_Stream % m_Destination;
					_Stream % m_SearchReplace;
				}

				CStr m_Pattern;
				CStr m_Destination;
				TCVector<CSearchReplace> m_SearchReplace;
			};

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
			CStr m_StaticSourcePath;
			CStr m_StaticUriRegex;
			CStr m_PackageSubDir;
			CStr m_MainFile;
			CStr m_ExternalRoot;
			CStr m_UploadS3Prefix;
			TCVector<CRedirect> m_RedirectsTemporary;
			TCVector<CRedirect> m_RedirectsPermanent;
			TCVector<CAlternateSource> m_AlternateSources;
			TCVector<CStr> m_ExcludeGzipPatterns;
			TCMap<CStr, int64> m_UploadS3Priority;
			fp64 m_MemoryPerNode = 1.5;
			mint m_Concurrency = 1;
			mint m_PortConcurrency = 1;
			EPackageType m_Type = EPackageType_Meteor;
			bool m_bSeparateUser = false;
			bool m_bOwnPackageDirectory = false;
			bool m_bAllowRobots = true;
			bool m_bUploadS3 = false;
			bool m_bMalterlibDistributedApp = false;
			bool m_bUnixSocket = false;
			bool m_bDefaultServer = false;
			bool m_bUseSystemNode = false;
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
			CStr m_DatabaseSetupPackage;
			CStr m_DefaultDatabase;
			CStr m_DefaultReplicaName = "DefaultReplica";
			CStr m_DefaultMongoVersion = "6.0";
		};

		CWebAppManagerOptions(CStr const &_ManagerName, CStr const &_ManagerDescription);
		void f_ParseSettings(CStr const &_Settings, CStr const &_FileName);

		CStr m_ManagerName;
		CStr m_FullManagerName;
		CStr m_ManagerDescription;
		CStr m_UserNamePrefix;
		TCMap<CStr, CPackage> m_Packages;
		TCVector<CStr> m_RobotsAllow;
		TCVector<CStr> m_RobotsDisallow;
		TCMap<CStr, CEnvironmentVariable> m_Environment;
		CMongo m_Mongo;
		CStr m_DefaultDomain;
		CStr m_HTTPRedirectReferrerCookie;
		CStr m_S3BucketPrefix;
		CStr m_RobotsSitemap;
		TCVector<CStr> m_AllowRedirectsOutsideOfDomainPatterns;
		CStr m_UniqueNginxPath;

		CStr m_ContentSecurity_DefaultSrc;
		CStr m_ContentSecurity_PrefetchSrc;
		CStr m_ContentSecurity_ScriptSrc;
		CStr m_ContentSecurity_ImgSrc;
		CStr m_ContentSecurity_MediaSrc;
		CStr m_ContentSecurity_FontSrc;
		CStr m_ContentSecurity_StyleSrc;
		CStr m_ContentSecurity_FrameSrc;
		CStr m_ContentSecurity_ConnectSrc;
		CStr m_ContentSecurity_ObjectSrc;
		CStr m_ContentSecurity_ChildSrc;
		CStr m_ContentSecurity_ManifestSrc;
		CStr m_ContentSecurity_FormAction;
		CStr m_ContentSecurity_ReportURI;
		CStr m_ContentSecurity_FrameAncestors;

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
		bool m_bAllowRobots = false;
		bool m_bNeedsMongo = false;
		bool m_bAllowRedirectsOutsideOfDomain = true;
		bool m_bAllowUnelevated = false;
	};

	struct ICWebAppManager
	{
		ICWebAppManager(CDistributedAppState const &_AppState, CWebAppManagerOptions const &_Options, TCSet<CStr> const &_Tags);
		virtual ~ICWebAppManager();

		virtual CEJsonSorted f_GetConfigValue(CStr const &_Name, CEJsonSorted const &_Default) const = 0;
		virtual NWeb::NHTTP::CURL f_GetMongoAddressURL(CStr _Database, CStr _HomePath) const = 0;

		CDistributedAppState const &m_AppState;
		CWebAppManagerOptions const &m_Options;
		TCSet<CStr> const &m_Tags;
	};

	struct CWebAppManagerActor;

	struct CWebAppManagerImpl : public ICWebAppManager
	{
		CWebAppManagerImpl(CWebAppManagerActor *_pThis);

		CEJsonSorted f_GetConfigValue(CStr const &_Name, CEJsonSorted const &_Default) const;
		NWeb::NHTTP::CURL f_GetMongoAddressURL(CStr _Database, CStr _HomePath) const;

	private:
		CWebAppManagerActor *mp_pThis = nullptr;
	};

	struct ICWebAppManagerCustomization
	{
		ICWebAppManagerCustomization();
		virtual ~ICWebAppManagerCustomization();
		virtual void f_SetupPrerequisites(TCSet<CStr> const &_Tags, TCMap<CStr, CUser> const &_Users);
		virtual void f_CalculateSettings
			(
				TCMap<CStr, CStr> &o_Settings
				, CJsonSorted &o_MeteorSettings
				, CStr const &_PackageName
				, CWebAppManagerOptions::CPackage const &_PackageOptions
				, ICWebAppManager const &_WebAppManager
			)
		;
		virtual void f_ManipulateNginxConfig
			(
				CStr &o_Config
				, CStr const &_FastCGIFile
				, TCMap<CStr, CStr> const &_PackageIPs
				, ICWebAppManager const &_WebAppManager
			)
		;
		virtual CStr f_DoStringReplacements(CStr const &_Headers, ICWebAppManager const &_WebAppManager);
		virtual TCUnsafeFuture<void> f_SetupPrerequisitesAsync(TCSet<CStr> _Tags, TCMap<CStr, CUser> _Users);
	};

	TCSharedPointer<ICWebAppManagerCustomization> fg_CreateWebAppManagerCustomization();

	struct CWebAppManagerActor : public CActor
	{
	public:
		using CActorHolder = CDelegatedActorHolder;

		friend struct CWebAppManagerImpl;

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
				CStr _Executable
				, CStr _WorkingDir
				, TCVector<CStr> _Params
				, CStr _LogCategory
				, ELogVerbosity _LogVerbosity
				, TCMap<CStr, CStr> _Environment = {}
				, bool _bSeparateStdErr = true
				, CStr _Home = {}
				, CStr _User = {}
				, CStr _Group = {}
#ifdef DPlatformFamily_Windows
				, CStrSecure _UserPassword = {}
#endif
			)
		;
		TCFuture<CStr> f_ExtractTar(CStr const &_TarFile, CStr const &_DestinationDir);

		TCFuture<void> f_InvalidateCloudFrontCaches();
		TCFuture<uint32> f_LaunchAsApp(NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine, CStr _Application, CStr _Executable, TCVector<CStr> _Params, CStr _WorkingDir);

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

			auto operator <=> (CAppLaunchKey const &_Right) const = default;
		};

		struct CNormalProcessLaunch
		{
			TCActor<CProcessLaunchActor> m_Launch;
			CActorSubscription m_Subscription;
		};

		struct CLaunchEnvironment
		{
			CSystemEnvironment m_Environment;
			NStr::CStr m_RunAsUser;
			NStr::CStrSecure m_RunAsUserPassword;
			NStr::CStr m_RunAsGroup;
		};

		struct CAppLaunch
		{
			CAppLaunchKey const &f_GetKey() const
			{
				return TCMap<CAppLaunchKey, CAppLaunch>::fs_GetKey(*this);
			}

			ch8 const *f_PortDelim() const
			{
				return m_bUnixSocket ? "." : ":";
			}

			TCVariant<void, CNormalProcessLaunch, TCUniquePointer<CDistributedApp_LaunchInfo>> m_Launch;
			TCOptional<CLaunchEnvironment> m_LaunchEnvironment;
			CActorSubscription m_TunnelSubscription;
			CStr m_LogCategory;
			CStr m_BackendIdentifier;
			TCOptional<TCPromise<void>> m_DestroyPromise;
			bool m_bInitialLaunched = false;
			bool m_bMalterlibDistributedApp = false;
			bool m_bUnixSocket = false;
		};

		ICWebAppManager const &fp_GetImpl();

		TCFuture<void> fp_Destroy() override;

		void fp_ParseConfig_DDPSelf();
		TCFuture<void> fp_ParseConfig();

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
		TCFuture<void> fp_SaveUserPassword(CStr _User, CStrSecure _Password);
#endif
		static void fsp_SetupUser
			(
				CUser &_User
				, bool _bRunningElevated
#ifdef DPlatformFamily_Windows
				, CStrSecure &o_Password
#endif
			)
		;
		TCFuture<void> fp_ExtractExeFS() const;
		TCFuture<void> fp_CheckVersion(CStr _Tool, CStr _Argument, CStr _ParseString, CVersion _NeededVersion);
		TCFuture<void> fp_CleanupOldProcesses();
		CStr fp_GetNodeExecutable(CStr const &_Executable, bool _bUseSystemNode);

		CEJsonSorted fp_GetConfigValue(CStr const &_Name, CEJsonSorted const &_Default) const;

		mint fp_GetNumNodes() const;
		mint fp_NeedsLocalIPs() const;

		static CHashDigest_MD5 fsp_GetFileChecksum(CStr const &_File);
		static void fsp_SetupPrerequisites_ServerUser
			(
				CUser &_User
				, bool _bRunningElevated
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
		TCFuture<void> fp_SetupPrerequisites_NginxUser();
		TCFuture<void> fp_SetupPrerequisites_Nginx();
		TCFuture<void> fp_SetupPrerequisites_Customization();
		TCFuture<void> fp_SetupPrerequisites_NodeExtract();
		TCFuture<void> fp_SetupPrerequisites_Mongo();
		TCFuture<void> fp_SetupPrerequisites_Packages();
		TCFuture<void> fp_SetupPrerequisites_UploadS3();
		TCFuture<void> fp_SetupPrerequisites_UploadS3Perform();
		TCFuture<void> fp_SetupPrerequisites_UploadS3FileChangeNotifications();
		TCFuture<void> fp_SetupPrerequisites_UpdateAWSLambda(CAwsCredentials _AWSCredentials, CStr _Prefix);
		TCFuture<void> fp_SetupPrerequisites_Package(CStr _PackageName, CWebAppManagerOptions::EPackageType _Type);
		TCFuture<void> fp_SetupPrerequisites_OSSetup();

		bool fp_FormatAlternateSources(CStr &o_Str, TCVector<CWebAppManagerOptions::CPackage::CAlternateSource> const &_AlternateSources);
		bool fp_FormatAlternateSourcesSearchReplace(CStr &o_Str, TCVector<CWebAppManagerOptions::CPackage::CAlternateSource> const &_AlternateSources);

		CStr fp_GetAllowRobots(bool _bAllow);

		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		CStr fp_GetMongoSSLDirectory() const;
		NWeb::NHTTP::CURL fp_GetDBAddressURL(CStr _Database, CStr _HomePath);
		CStr fp_GetDBAddress(CStr _Database, CStr _HomePath);
		TCFuture<void> fp_RunNodeMongoScript(CStr _ScriptName, CStr _Script, TCVector<CStr> _Params, CStr _Database, fp32 _Timeout);

		TCFuture<void> fp_SetupMongo();

		static CStr fsp_GetVersionString();
		TCFuture<void> fp_UpdateVersionHistory();
		TCFuture<void> fp_InvalidateCloudfrontDistributionsWithRetry(TCSet<CStr> _Distributions);
		TCFuture<void> fp_InvalidateCloudfrontDistributions();

		CStr fp_GetPackageRoot(CStr const &_PackageName) const;
		CStr fp_GetPackageSocketRoot(CStr const &_PackageName) const;
		CStr fp_GetPackageHostname(CStr const &_PackageName, EHostnamePrefix _Prefix) const;
		CStr fp_GetPackageLocalURL(CStr const &_PackageName) const;
		CStr fp_GetRootURL(CStr const &_Hostname, CStr const &_SubPath) const;
		CStr fp_GetAppIPAddress(CAppLaunch const &_AppLaunch, bool _bForMalterlib) const;
		CStr fp_GetAppLocalURL(CAppLaunch const &_AppLaunch, mint _iPort) const;
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

		CStr fp_DoCustomStringReplacements(CStr const &_String);

		TCSharedPointer<CUniqueUserGroup> mp_pUniqueUserGroup;

		CWebAppManagerOptions mp_Options;

		TCSharedPointer<ICWebAppManagerCustomization> mp_pCustomization;

		CDistributedAppState &mp_AppState;

		TCActor<CFileChangeNotificationActor> mp_FileChangeNotificationActor = fg_Construct();
		TCVector<CActorSubscription> mp_S3FileChangeNotificationSubscriptions;

		CSequencer mp_S3UploadSequencer{"WebAppManagerActor S3UploadSequencer"};
		CSequencer mp_S3FileReadSequencer{"WebAppManagerActor S3FileReadSequencer", 8};
		CSequencer mp_S3PrioritySequencer{"WebAppManagerActor S3PrioritySequencer"};
		CSequencer mp_S3DeleteSequencer{"WebAppManagerActor S3DeleteSequencer", 32};
		TCSequencer<CAwsS3Actor::CObjectInfoMetadata> mp_S3MetadataSequencer{"WebAppManagerActor S3MetadataSequencer", 32};

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

		TCActor<CDistributedApp_LaunchHelper> mp_AppLaunchHelper;
		TCMap<CAppLaunchKey, CAppLaunch> mp_AppLaunches;
		TCMap<CStr, zmint> mp_OutstandingLaunches;
		mutable TCMap<CStr, zmint> mp_CurrentPackageLocalURL;
		TCMap<CStr, TCVector<CStr>> mp_PackageLocalURLs;
		mint mp_AppSequence = 0;
		TCPromise<void> mp_AppLaunchesPromise;

		CStr mp_InstanceId;

		TCActor<CProcessLaunchActor> mp_NginxLaunch;
		CActorSubscription mp_NginxLaunchSubscription;

		TCRoundRobinActors<CCurlActor> mp_CurlActors{4};
		TCRoundRobinActors<CAwsS3Actor> mp_S3Actors{4};

		TCSet<CStr> mp_LastCloudFrontDistributions;

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
		CStr mp_DomainCookie;
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
		CStr mp_MongoVersion = "6.0";
		bool mp_bConnectToExternalMongo = false;
		TCVector<CMongoServerHost> mp_ExternalMongoHosts;
		CStr mp_MongoReplicaNameExternal;

		// Mongo certificate deploy
		TCActor<CMongoCertificateDeployActor> mp_MongoCertificateDeployActor;
		CActorSubscription mp_MongoCertificateDeploySubscription_Admin;

		CStr mp_BindTo;

		TCUniquePointer<CWebAppManagerImpl> mp_pWebAppManangerImpl;

		CStr mp_UniqueNginxPath;

		uint64 mp_WebPort = 3000;
		uint64 mp_WebSSLPort = 3443;
		uint16 mp_LocalPort = 8080;
		uint8 mp_LoopbackPrefix = 0;
		bool mp_bIsStaging = false;
		bool mp_bAllowRobots = true;
		bool mp_bStartNginx = true;
		bool mp_bEnableIPV6 = true;
		bool mp_bCheckForInvalidHost = true;
		bool mp_bRunningElevated = false;
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NWebApp::NWebAppManager;
#endif

#else
#include <stdlib.h>
#endif
