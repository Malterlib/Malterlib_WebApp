
#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/BackupManager>

namespace NMib::NMeteor::NMeteorManager
{
	namespace
	{
		void fg_CleanupOldProcesses(TCVector<CStr> const &_Executables)
		{
			mint nKilled = 0;
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

	TCFuture<void> CMeteorManagerActor::fp_CleanupOldProcesses()
	{
		TCVector<CStr> Executables;
		for (auto &PackageOptions : fg_Const(mp_Options.m_Packages))
		{
			if (PackageOptions.f_HasCustomExecutable() && !PackageOptions.m_CustomExecutable.f_IsEmpty())
				Executables.f_Insert(CFile::fs_GetFileNoExt(PackageOptions.m_CustomExecutable));
		}

		co_await
			(
			 	g_Dispatch(*mp_FileActors) / [Executables = fg_Move(Executables)]
				{
					fg_CleanupOldProcesses(Executables);
				}
			)
		;

		co_return {};

	}
}
