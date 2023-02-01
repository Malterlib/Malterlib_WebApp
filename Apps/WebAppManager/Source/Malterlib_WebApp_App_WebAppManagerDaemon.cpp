// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManagerDaemon.h"
#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	CWebAppManagerDaemonActor::CWebAppManagerDaemonActor(CWebAppManagerOptions const &_Options)
		: CDistributedAppActor(CDistributedAppActor_Settings{_Options.m_FullManagerName}.f_CommandLineBeforeAppStart(true))
		, mp_Options(_Options)
	{
#ifdef DPlatformFamily_macOS
		CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");

		CStr OriginalPath = Path;

		if (Path.f_Find("/usr/local/bin") < 0)
			Path = "/usr/local/bin:" + Path;
		if (Path.f_Find("/opt/homebrew/bin") < 0)
			Path = "/opt/homebrew/bin:" + Path;

		if (Path != OriginalPath)
			fg_GetSys()->f_SetEnvironmentVariable("PATH", Path);
#endif
		auto ProgramDirectory = CFile::fs_GetProgramDirectory();

		auto Files = CFile::fs_FindFiles(ProgramDirectory + "/node-*.tar.gz");
		if (!Files.f_IsEmpty())
			mp_Options.m_bUseInternalNode = true;

		if (mp_Options.m_bUseInternalNode)
		{
			CStr NodeBinDirectory = ProgramDirectory + "/node_dist/bin";
			CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");
			fg_GetSys()->f_SetEnvironmentVariable("PATH", NodeBinDirectory + ":" + Path);
		}
	}

	CWebAppManagerDaemonActor::~CWebAppManagerDaemonActor()
	{
	}

	void CWebAppManagerDaemonActor::fp_PopulateAppInterfaceRegisterInfo(CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo, NEncoding::CEJSON const &_Params)
	{
		o_RegisterInfo.m_UpdateType = EDistributedAppUpdateType_AllAtOnce;

		mint nNodes = 0;

		for (auto &Package : mp_Options.m_Packages)
			nNodes += Package.m_Concurrency;

		mint nMaxFilesNeeded = 8192;
		nMaxFilesNeeded += CWebAppManagerActor::fs_GetNginxFileLimits(nNodes);
		nMaxFilesNeeded += CWebAppManagerActor::fs_GetNodeFileLimits() * nNodes;

		mint nFilesPerProc = 0;
		nFilesPerProc = fg_Max(nFilesPerProc, CWebAppManagerActor::fs_GetNginxWorkerFileLimits());
		nFilesPerProc = fg_Max(nFilesPerProc, CWebAppManagerActor::fs_GetNodeFileLimits());

		mint nMaxThreads = 1024;

		mint nMaxPids = 32 + nNodes;

		o_RegisterInfo.m_Resources_Files = nMaxFilesNeeded;
		o_RegisterInfo.m_Resources_Threads = nMaxThreads;
		o_RegisterInfo.m_Resources_FilesPerProcess = nFilesPerProc;
		o_RegisterInfo.m_Resources_Processes = nMaxPids;
	}

	TCFuture<void> CWebAppManagerDaemonActor::fp_StartApp(NEncoding::CEJSON const &_Params)
	{
		mp_pManager = fg_ConstructActor<CWebAppManagerActor>(fg_Construct(self), mp_State, mp_Options);

		co_return co_await mp_pManager(&CWebAppManagerActor::f_Startup);
	}

	TCFuture<void> CWebAppManagerDaemonActor::fp_StopApp()
	{
		if (mp_pManager)
		{
			DMibLogWithCategory(Daemon, Info, "Shutting down");
			co_await (fg_Move(mp_pManager).f_Destroy() % "Failed to shut down server");
		}

		co_return {};
	}

	TCFuture<void> CWebAppManagerDaemonActor::fp_PreStop()
	{
		if (!mp_pManager)
			co_return  {};

		DMibLogWithCategory(Daemon, Info, "Running pre-stop");

		co_await (mp_pManager(&CWebAppManagerActor::f_PreStop) % "Failed to pre-stop down server");

		co_return {};
	}
}
