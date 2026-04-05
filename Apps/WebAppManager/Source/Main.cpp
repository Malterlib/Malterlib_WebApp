// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Core/Application>
#include <Mib/Daemon/Daemon>
#include <Mib/Concurrency/DistributedDaemon>

#include "Malterlib_WebApp_App_WebAppManagerDaemon.h"

using namespace NMib;
using namespace NMib::NWebApp::NWebAppManager;

ch8 g_Settings[] =
#include DMibWebAppManager_SettingsPath
;

class CWebAppManagerApp : public CApplication
{
	aint f_Main()
	{
		CWebAppManagerOptions Options{DMibWebAppManager_ManagerName, DMibWebAppManager_ManagerDescription};
#ifdef DMibWebAppManager_UseInternalNode
		Options.m_bUseInternalNode = true;
#endif
		try
		{
			Options.f_ParseSettings(g_Settings, DMibWebAppManager_SettingsPath);
		}
		catch (NException::CException const &_Exception)
		{
			DMibConErrOut("Error parsing settings: {}\n", _Exception);
			return 1;
		}

		NConcurrency::CDistributedDaemon Daemon
			{
				DMibWebAppManager_ManagerDaemonName
				, DMibWebAppManager_ManagerDescription
				, "Manages web applications"
				, [Options]
				{
					return fg_ConstructActor<CWebAppManagerDaemonActor>(Options);
				}
			}
		;
		return Daemon.f_Run();
	}
};

DAppImplement(CWebAppManagerApp);
