// Copyright © 2020 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Core/Application>

#ifdef DPlatformFamily_Windows
#include <Windows.h>
#endif

#include "Malterlib_WebApp_App_AcmeManager.h"

using namespace NMib;
using namespace NMib::NWebApp::NAcmeManager;

class CAcmeManager : public CApplication
{
	aint f_Main()
	{
		NConcurrency::CDistributedDaemon Daemon
			{
				"MalterlibWebAppAcmeManager"
				, "Malterlib ACME Manager"
				, "Manages issues and reissues certificates through ACME"
				, []
				{
					return fg_ConstructActor<CAcmeManagerActor>();
				}
			}
		;

		return Daemon.f_Run();
	}
};

DAppImplement(CAcmeManager);
