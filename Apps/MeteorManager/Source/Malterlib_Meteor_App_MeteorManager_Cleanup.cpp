
#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/BackupManager>

namespace NMib::NMeteor::NMeteorManager
{
	namespace
	{
		void fg_CleanupOldProcesses()
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
			if (nKilled)
				DLog(Error, "Cleaned up {} old processes", nKilled);
		}
	}

	TCContinuation<void> CMeteorManagerActor::fp_CleanupOldProcesses()
	{
		return g_Dispatch(*mp_FileActors) > []
			{
				fg_CleanupOldProcesses();
			}
		;
	}
}
