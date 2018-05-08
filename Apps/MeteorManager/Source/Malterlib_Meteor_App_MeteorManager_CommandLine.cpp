// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>

#include "Malterlib_Meteor_App_MeteorManagerDaemon.h"
#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	void CMeteorManagerDaemonActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				mp_Options.m_ManagerDescription
				, "Manages web applications."
			)
		;
		
		//auto DefaultSection = o_CommandLine.f_GetDefaultSection();
	}
}
