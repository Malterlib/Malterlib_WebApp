// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>

#include "Malterlib_WebApp_App_WebAppManagerDaemon.h"
#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	void CWebAppManagerDaemonActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				mp_Options.m_ManagerDescription
				, "Manages web applications."
			)
		;

		auto DefaultSection = o_CommandLine.f_GetDefaultSection();

		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_o= {"--invalidate-cloud-front-caches"}
					, "Description"_o= "Invalidate all CloudFront caches.\n"
				}
				, [this] (NEncoding::CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine) -> TCFuture<uint32>
				{
					co_await fp_WaitForAppStartup();

					*_pCommandLine %= "Invalidating\n";

					co_await mp_pManager(&CWebAppManagerActor::f_InvalidateCloudFrontCaches);

					*_pCommandLine %= "Done\n";

					co_return 0;
				}
				, EDistributedAppCommandFlag_WaitForRemotes
			)
		;
	}
}
