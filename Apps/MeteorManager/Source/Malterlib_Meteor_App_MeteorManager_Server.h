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
		struct CPackage
		{
			mint m_Concurrency = 1;
			fp64 m_MemoryPerNode = 1.5;
			TCVector<CStr> m_StartupDependencies;
		};
		
		void f_ParseSettings(CStr const &_Settings, CStr const &_FileName);
		
		CStr m_ManagerName;
		TCMap<CStr, CPackage> m_Packages;
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
		
		mint fp_GetNumNodes() const;
		
		TCContinuation<void> fp_SetupPrerequisites_Node();
		TCContinuation<void> fp_SetupPrerequisites_NodeExtract();
		TCContinuation<void> fp_SetupPrerequisites_OSSetup();
		
		TCContinuation<void> fp_StartApps();
		TCContinuation<void> fp_DestroyApps();
		
		CMeteorManagerOptions mp_Options;
		TCActor<CSeparateThreadActor> mp_pFileActor;
		
		TCUniquePointer<ICMeteorManagerCustomization> mp_pCustomization;
		
		TCSharedPointer<CCanDestroyTracker> mp_pCanDestroyTracker;
		CDistributedAppState &mp_AppState;

		CUser mp_NodeUser;
		CVersion mp_Version_Node{0, 10, 33};

		bool mp_bStopped = false;
		
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
