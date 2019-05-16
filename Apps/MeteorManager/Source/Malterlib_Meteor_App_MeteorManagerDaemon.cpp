// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManagerDaemon.h"
#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	CMeteorManagerDaemonActor::CMeteorManagerDaemonActor(CMeteorManagerOptions const &_Options)
		: CDistributedAppActor(CDistributedAppActor_Settings{_Options.m_FullManagerName})
		, mp_Options(_Options)
	{
#ifdef DPlatformFamily_OSX
		CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");
		if (Path.f_Find("/usr/local/bin") < 0)
			fg_GetSys()->f_SetEnvironmentVariable("PATH", "/usr/local/bin:" + Path);
#endif
		if (mp_Options.m_bUseInternalNode)
		{
			CStr NodeBinDirectory = CFile::fs_GetProgramDirectory() + "/node_dist/bin";
			CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");
			fg_GetSys()->f_SetEnvironmentVariable("PATH", NodeBinDirectory + ":" + Path);
		}
	}

	CMeteorManagerDaemonActor::~CMeteorManagerDaemonActor()
	{
	}

	void CMeteorManagerDaemonActor::fp_PopulateAppInterfaceRegisterInfo(CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo, NEncoding::CEJSON const &_Params)
	{
		o_RegisterInfo.m_UpdateType = EDistributedAppUpdateType_AllAtOnce;

		mint nNodes = 0;

		for (auto &Package : mp_Options.m_Packages)
			nNodes += Package.m_Concurrency;

		mint nMaxFilesNeeded = 8192;
		nMaxFilesNeeded += CMeteorManagerActor::fs_GetNginxFileLimits(nNodes);
		nMaxFilesNeeded += CMeteorManagerActor::fs_GetNodeFileLimits() * nNodes;

		mint nFilesPerProc = 0;
		nFilesPerProc = fg_Max(nFilesPerProc, CMeteorManagerActor::fs_GetNginxWorkerFileLimits());
		nFilesPerProc = fg_Max(nFilesPerProc, CMeteorManagerActor::fs_GetNodeFileLimits());

		mint nMaxThreads = 1024;

		mint nMaxPids = 32 + nNodes;

		o_RegisterInfo.m_Resources_Files = nMaxFilesNeeded;
		o_RegisterInfo.m_Resources_Threads = nMaxThreads;
		o_RegisterInfo.m_Resources_FilesPerProcess = nFilesPerProc;
		o_RegisterInfo.m_Resources_Processes = nMaxPids;
	}

	TCFuture<void> CMeteorManagerDaemonActor::fp_StartApp(NEncoding::CEJSON const &_Params)
	{
		mp_pManager = fg_ConstructActor<CMeteorManagerActor>(fg_Construct(self), mp_State, mp_Options);

		return mp_pManager(&CMeteorManagerActor::f_Startup);
	}

	TCFuture<void> CMeteorManagerDaemonActor::fp_StopApp()
	{
		TCSharedPointer<CCanDestroyTracker> pCanDestroy = fg_Construct();

		if (mp_pManager)
		{
			DMibLogWithCategory(Daemon, Info, "Shutting down");

			mp_pManager->f_Destroy() > [pCanDestroy](TCAsyncResult<void> &&_Result)
				{
					if (!_Result)
						DMibLogWithCategory(Daemon, Error, "Failed to shut down server: {}", _Result.f_GetExceptionStr());
				}
			;
			mp_pManager = nullptr;
		}

		return pCanDestroy->f_Future();
	}

	TCFuture<void> CMeteorManagerDaemonActor::fp_PreStop()
	{
		if (!mp_pManager)
			return fg_Explicit();

		DMibLogWithCategory(Daemon, Info, "Running pre-stop");

		TCPromise<void> Promise;
		mp_pManager(&CMeteorManagerActor::f_PreStop) > [Promise](TCAsyncResult<void> &&_Result)
			{
				if (!_Result)
					DMibLogWithCategory(Daemon, Error, "Failed to pre-stop down server: {}", _Result.f_GetExceptionStr());
				Promise.f_SetResult();
			}
		;

		return Promise.f_MoveFuture();
	}
}
