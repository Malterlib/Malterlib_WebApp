// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Core/Application>
#include <Mib/Daemon/Daemon>
#include <Mib/Concurrency/DistributedDaemon>

#include "Malterlib_Meteor_App_MeteorManagerDaemon.h"

using namespace NMib;
using namespace NMib::NMeteor::NMeteorManager;

ch8 g_Settings[] =
#include DMibMeteor_SettingsPath
;

class CMeteorManagerApp : public CApplication
{
	aint f_Main()
	{
		CMeteorManagerOptions Options{DMibMeteor_ManagerName};
#ifdef DMibMeteor_UseInternalNode
		Options.m_bUseInternalNode = true;
#endif
		try
		{
			Options.f_ParseSettings(g_Settings, DMibMeteor_SettingsPath);
		}
		catch (NException::CException const &_Exception)
		{
			DMibConErrOut("Error parsing settings: {}\n", _Exception);
			return 1;
		}
		
		NConcurrency::CDistributedDaemon Daemon
			{
				fg_Format("MalterlibMeteorManager_{}", DMibMeteor_ManagerName)
				, fg_Format("Malterlib Meteor Manager ({})", DMibMeteor_ManagerName)
				, "Manages meteor applications"
				, [Options]
				{
					return fg_ConstructActor<CMeteorManagerDaemonActor>(Options);
				}
			}
		;
		return Daemon.f_Run();
	}	
};

DAppImplement(CMeteorManagerApp);
