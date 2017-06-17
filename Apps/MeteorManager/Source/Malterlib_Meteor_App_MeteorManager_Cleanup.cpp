
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

			// Kill individual processes
			nKilled += CProcessLaunch::fs_KillProcessesInDirectory("node");
			if (nKilled)
				DLog(Error, "Cleaned up {} old processes", nKilled);
		}
	}

	TCContinuation<void> CMeteorManagerActor::fp_CleanupOldProcesses()
	{
		return fg_Dispatch
			(
				mp_pFileActor
				, []
				{
					fg_CleanupOldProcesses();
				}
			)
		;
	}
}
