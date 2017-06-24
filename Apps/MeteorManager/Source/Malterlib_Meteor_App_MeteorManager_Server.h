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
		
		struct CPackage
		{
			CStr const &f_GetName() const
			{
				return TCMap<CStr, CPackage>::fs_GetKey(*this);
			}
			
			EPackageType m_Type = EPackageType_Meteor;
			mint m_Concurrency = 1;
			fp64 m_MemoryPerNode = 1.5;
			TCVector<CStr> m_StartupDependencies;
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
		};
		
		CMeteorManagerOptions(CStr const &_ManagerName);
		void f_ParseSettings(CStr const &_Settings, CStr const &_FileName);
		
		CStr m_ManagerName;
		TCMap<CStr, CPackage> m_Packages;
		CMongo m_Mongo;
		uint8 m_LoopbackPrefix = 0;
		bool m_bUseInternalNode = false;
	};
	
	struct ICMeteorManagerCustomization
	{
		ICMeteorManagerCustomization();
		virtual ~ICMeteorManagerCustomization();
		virtual void f_SetupNodeEnvironment(CSystemEnvironment &o_Environment, CStr const &_PackageName, CDistributedAppState const &_AppState, CMeteorManagerOptions const &_Options);
	};
	
	TCUniquePointer<ICMeteorManagerCustomization> fg_CreateMeteorManagerCustomization();
	
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
		TCContinuation<void> fp_Destroy() override;
		
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
		
		TCContinuation<void> fp_SetupPrerequisites_Node();
		TCContinuation<void> fp_SetupPrerequisites_NodeExtract();
		TCContinuation<void> fp_SetupPrerequisites_Packages();
		TCContinuation<void> fp_SetupPrerequisites_Package(CStr const &_BundleName, CMeteorManagerOptions::EPackageType _Type);
		TCContinuation<void> fp_SetupPrerequisites_OSSetup();
		
		CStr fp_GetMongoExecutable(CStr const &_ExecutableName) const;
		TCContinuation<void> fp_RunMongoScript(CStr const &_Script, CStr const &_Database, fp32 _Timeout);
		
		TCContinuation<void> fp_SetupMongo();
		
		static CStr fsp_GetVersionString();
		TCContinuation<void> fp_UpdateVersionHistory();
		
		TCContinuation<void> fp_StartApps();
		TCContinuation<void> fp_DestroyApps();
		
		CMeteorManagerOptions mp_Options;
		TCRoundRobinActors<CSeparateThreadActor> mp_FileActors;
		
		TCUniquePointer<ICMeteorManagerCustomization> mp_pCustomization;
		
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState &mp_AppState;

		CUser mp_NodeUser;
		CVersion mp_Version_Node{0, 10, 33};
		TCVector<CStr> mp_VersionHistory;

		bool mp_bStopped = false;
		bool mp_bForceAppsReinstall = false;
		
		// Tool launches
		TCLinkedList<CToolLaunch> mp_ToolLaunches;
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NMeteor::NMeteorManager;
#endif

#else
#include <stdlib.h>
#endif
