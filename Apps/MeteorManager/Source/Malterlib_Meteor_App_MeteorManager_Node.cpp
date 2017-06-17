// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMeteor::NMeteorManager
{
	CStr CMeteorManagerActor::fp_GetNodeExecutable(CStr const &_Executable)
	{
		if (mp_Options.m_bUseInternalNode)
		{
			CStr NodeBinDirectory = CFile::fs_GetProgramDirectory() + "/node_dist/bin";
			//CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");
			//fg_GetSys()->f_SetEnvironmentVariable("PATH", NodeBinDirectory + ":" + Path);
			return NodeBinDirectory + "/node";
		}
		else
			return "node";
	}

	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Node()
	{
		return {};
	}

	TCContinuation<void> CMeteorManagerActor::fp_StartApps()
	{
		return {};
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_DestroyApps()
	{
		return {};
	}
}
