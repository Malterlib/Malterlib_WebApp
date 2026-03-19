
#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/BackupManager>

namespace NMib::NWebApp::NWebAppManager
{
	namespace
	{
		void fg_CleanupOldProcesses(TCVector<CStr> const &_Executables)
		{
			umint nKilled = 0;
			// Kill old managers
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory(CFile::fs_GetFile(CFile::fs_GetProgramPath()));

			auto fAddExtension = [](CStr const &_File)
				{
#ifdef DPlatformFamily_Windows
					return _File + ".exe";
#else
					return _File;
#endif
				}
			;

			// Kill individual processes
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory(fAddExtension("nginx"), "*master*");
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory(fAddExtension("nginx"));
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory(fAddExtension("node"));
			for (auto &Executable : _Executables)
				nKilled += CProcessLaunch::fs_KillProcessesInDirectory(fAddExtension(Executable));

			if (nKilled)
				DLog(Error, "Cleaned up {} old processes", nKilled);
		}
	}

	TCFuture<void> CWebAppManagerActor::fp_CleanupOldProcesses()
	{
		TCVector<CStr> Executables;
		for (auto &PackageOptions : fg_Const(mp_Options.m_Packages))
		{
			if (PackageOptions.f_HasCustomExecutable() && !PackageOptions.m_CustomExecutable.f_IsEmpty())
				Executables.f_Insert(CFile::fs_GetFileNoExt(PackageOptions.m_CustomExecutable));
		}

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await BlockingActorCheckout.f_Actor().f_Bind<fg_CleanupOldProcesses>(fg_Move(Executables));

		co_return {};

	}
}
