// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#ifdef __cplusplus

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Mongo/Client>
#include <Mib/Storage/Optional>

#include "Malterlib_Meteor_App_MeteorManager_Helpers.h"

namespace NMib::NMeteor::NMeteorManager
{
	struct CMeteorManagerOptions
	{
		enum EPackageType
		{
			EPackageType_Meteor
			, EPackageType_Npm
			, EPackageType_Custom
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
			
			TCVector<CStr> m_StartupDependencies;
			CStr m_CustomExecutable;
			TCVector<CStr> m_CustomParams;
			CUser m_User{""};
			CStr m_DomainPrefix;
			CStr m_RedirectsFile;
			CStr m_StickyCookie;
			CStr m_StickyHeader;
			CStr m_StaticPath;
			fp64 m_MemoryPerNode = 1.5;
			mint m_Concurrency = 1;
			EPackageType m_Type = EPackageType_Meteor;
			bool m_bSeparateUser = false;
			bool m_bAllowRobots = true;
		};
		
		struct CMongo
		{
			CStr m_Directory = "../MongoManager/mongo/bin";
			CStr m_Host = NProcess::NPlatform::fg_Process_GetHostName();
			int16 m_Port = 25017;
			CStr m_ToolsUser = "mib_mongo";
			CStr m_ToolsGroup = "mib_mongo";
			CStr m_SSLDirectory;
			CStr m_DatabaseSetupScript;
			CStr m_DefaultDatabase;
			CStr m_DefaultReplicaName = "DefaultReplica";
		};
		
		CMeteorManagerOptions(CStr const &_ManagerName);
		void f_ParseSettings(CStr const &_Settings, CStr const &_FileName);
		
		CStr m_ManagerName;
		TCMap<CStr, CPackage> m_Packages;
		TCMap<CStr, CEnvironmentVariable> m_Environment;
		CMongo m_Mongo;
		CStr m_DefaultDomain;
		CStr m_HTTPRedirectReferrerCookie;
		uint16 m_DefaultWebPort = 3000;
		uint16 m_DefaultWebSSLPort = 3443;
		uint8 m_LoopbackPrefix = 0;
		bool m_bUseInternalNode = false;
		bool m_bRedirectWWW = false;
	};
	
	struct ICMeteorManagerCustomization
	{
		ICMeteorManagerCustomization();
		virtual ~ICMeteorManagerCustomization();
		virtual void f_SetupPrerequisites(TCSet<CStr> const &_Tags);
		virtual void f_CalculateSettings
			(
				TCMap<CStr, CStr> &o_Settings
				, CJSON &o_MeteorSettings
				, CStr const &_PackageName
				, CDistributedAppState const &_AppState
				, CMeteorManagerOptions const &_Options
				, CMeteorManagerOptions::CPackage const &_PackageOptions
				, TCFunction<CEJSON (CStr const &_Name, CEJSON const &_Default)> const &_fGetConfigValue
			)
		;
		virtual void f_ManipulateNginxConfig
			(
				CStr &o_Config
				, CDistributedAppState const &_AppState
				, CMeteorManagerOptions const &_Options
				, TCFunction<CEJSON (CStr const &_Name, CEJSON const &_Default)> const &_fGetConfigValue
				, TCSet<CStr> const &_Tags
			)
		;
	};
	
	TCSharedPointer<ICMeteorManagerCustomization> fg_CreateMeteorManagerCustomization();
	
	struct CMeteorManagerActor : public CActor
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
		
		CMeteorManagerActor(CDistributedAppState &_AppState, CMeteorManagerOptions const &_Options);
		~CMeteorManagerActor();
		TCContinuation<void> f_Startup();
		TCContinuation<void> f_PreStop();
		
		static void fs_SetupEnvironment(CProcessLaunchParams &_Params);
		
		static mint fs_GetNodeFileLimits();
		static mint fs_GetNginxWorkerFileLimits();
		static mint fs_GetNginxFileLimits(mint _nNodes);

		TCContinuation<CStr> f_LaunchTool
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
			)
		;
		TCContinuation<CStr> f_ExtractTar(CStr const &_TarFile, CStr const &_DestinationDir);
		
	private:
		
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
			CStr m_LogCategory;
			CStr m_BackendIdentifier;
			bool m_bInitialLaunched = false;
		};
		
		TCContinuation<void> fp_Destroy() override;
		
		void fp_ParseConfig_DDPSelf();
		void fp_ParseConfig();
		
		CStr fp_GetDataPath(CStr const &_Path) const;
		CStr fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const;
		TCContinuation<CStr> fp_RunToolForVersionCheck
			(
				CStr const &_Tool
				, TCVector<CStr> const &_Arguments
			)
		;
		static void fsp_SetupUser(CUser &_User);
		TCContinuation<void> fp_ExtractExeFS() const;
		TCContinuation<void> fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion);
		TCContinuation<void> fp_CleanupOldProcesses();
		CStr fp_GetNodeExecutable(CStr const &_Executable);
		
		CEJSON fp_GetConfigValue(CStr const &_Name, CEJSON const &_Default) const;
		
		mint fp_GetNumNodes() const;
		
		static CHashDigest_MD5 fsp_GetFileChecksum(CStr const &_File);
		static void fsp_SetupPrerequisites_NodeUser(CUser &_User, CStr const &_Directory, CStr const &_SSLDirectory);
		
		TCContinuation<void> fp_SetupPrerequisites_Node();
		TCContinuation<void> fp_SetupPrerequisites_Nginx();
		TCContinuation<void> fp_SetupPrerequisites_Customization();
		TCContinuation<void> fp_SetupPrerequisites_NodeExtract();
		TCContinuation<void> fp_SetupPrerequisites_Packages();
		TCContinuation<void> fp_SetupPrerequisites_Package(CStr const &_PackageName, CMeteorManagerOptions::EPackageType _Type);
		TCContinuation<void> fp_SetupPrerequisites_OSSetup();
		
		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		CStr fp_GetMongoSSLDirectory() const;
		TCContinuation<void> fp_RunMongoScript(CStr const &_Script, CStr const &_Database, fp32 _Timeout);
		
		TCContinuation<void> fp_SetupMongo();
		
		static CStr fsp_GetVersionString();
		TCContinuation<void> fp_UpdateVersionHistory();

		CStr fp_GetPackageHostname(CStr const &_PackageName, bool _bStatic) const;
		CStr fp_GetPackageLocalURL(CStr const &_PackageName) const;
		CStr fp_GetRootURL(CStr const &_Hostname) const;
		CStr fp_GetAppIPAddress(CAppLaunch const &_AppLaunch) const;
		CStr fp_GetAppLocalURL(CAppLaunch const &_AppLaunch) const;
		void fp_UpdateAppLaunch(CExceptionPointer const &_pException);
		void fp_LaunchApp(CAppLaunch &_AppLaunch, bool _bInitialLaunch);
		void fp_SetupNodeArguments(TCVector<CStr> &o_Arguments, CAppLaunch const &_AppLaunch, CMeteorManagerOptions::CPackage const &_PackageOptions);
		void fp_PopulateNodeEnvironment
			(
				CSystemEnvironment &o_Environment
				, TCMap<CStr, CStr> const &_CalculatedSettings
				, CAppLaunch const &_AppLaunch
				, CMeteorManagerOptions::CPackage const &_PackageOptions
			)
		;
		
		void fp_CreateAppLaunches();
		
		TCContinuation<void> fp_StartNginx();
		TCContinuation<void> fp_StartApps();
		TCContinuation<void> fp_DestroyApps();
		
		CMeteorManagerOptions mp_Options;
		TCRoundRobinActors<CSeparateThreadActor> mp_FileActors;
		
		TCSharedPointer<ICMeteorManagerCustomization> mp_pCustomization;
		
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState &mp_AppState;

		CUser mp_NodeUser;
		CUser mp_NginxUser;
		CVersion mp_Version_Node{0, 10, 33};
		TCVector<CStr> mp_VersionHistory;
		TCSet<CStr> mp_Tags;

		bool mp_bStopped = false;
		bool mp_bForceAppsReinstall = false;
		
		TCLinkedList<CToolLaunch> mp_ToolLaunches;
		
		TCMap<CAppLaunchKey, CAppLaunch> mp_AppLaunches;
		TCMap<CStr, zmint> mp_OutstandingLaunches;
		mutable TCMap<CStr, zmint> mp_CurrentPackageLocalURL;
		TCMap<CStr, TCVector<CStr>> mp_PackageLocalURLs;
		mint mp_AppSequence = 0;
		TCContinuation<void> mp_AppLaunchesContinuation;
		
		CStr mp_InstanceId;
		
		TCActor<CProcessLaunchActor> mp_NginxLaunch;
		CActorSubscription mp_NginxLaunchSubscription;
		
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
		
		uint64 mp_WebPort = 3000;
		uint64 mp_WebSSLPort = 3443;
		bool mp_bIsStaging = false;
		
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NMeteor::NMeteorManager;
#endif

#else
#include <stdlib.h>
#endif
