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
			return CFile::fs_GetProgramDirectory() + "/node_dist/bin/node";
		else
			return "node";
	}

	TCContinuation<void> CMeteorManagerActor::fp_StartApps()
	{
		return fg_Explicit();
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_DestroyApps()
	{
		return fg_Explicit();
	}
}
