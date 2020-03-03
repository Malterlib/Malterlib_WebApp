// Copyright © 2020 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Core/Application>

#ifdef DPlatformFamily_Windows
#include <Windows.h>
#endif

#include "Malterlib_WebApp_App_WebCertificateManager.h"

using namespace NMib;
using namespace NMib::NWebApp::NWebCertificateManager;

class CWebCertificateManager : public CApplication
{
	aint f_Main()
	{
		NConcurrency::CDistributedDaemon Daemon
			{
				"MalterlibWebAppWebCertificateManager"
				, "Malterlib Web Certificate Manager"
				, "Deploys certificates to file system from SecretsManager"
				, []
				{
					return fg_ConstructActor<CWebCertificateManagerActor>();
				}
			}
		;

		return Daemon.f_Run();
	}
};

DAppImplement(CWebCertificateManager);
