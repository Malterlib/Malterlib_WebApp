// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Encoding/JsonShortcuts>

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
					"Names"_o= _o["--invalidate-cloud-front-caches"]
					, "Description"_o= "Invalidate all CloudFront caches.\n"
				}
				, [this] (NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
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

		DefaultSection.f_RegisterCommand
			(
				{
					"Names"_o= _o["--launch-as-app"]
					, "Description"_o= "Launch an executable in the same environment as an app launch.\n"
					, "Options"_o=
					{
						"Application"_o=
						{
							"Names"_o= _o["--app"]
							, "Type"_o= ""
							, "Description"_o= "The app to launch as.\n"
						}
						, "WorkingDirectory"_o=
						{
							"Names"_o= _o[]
							, "Hidden"_o= true
							, "Default"_o= CFile::fs_GetCurrentDirectory()
							, "Description"_o= "Hidden internal working directory propagration.\n"
						}
					}
					, "Parameters"_o=
					{
						"Executable"_o=
						{
							"Default"_o= ""
							, "Description"_o= "The executable to launch."
						}
						,
						"Params...?"_o=
						{
							"Type"_o= _o[""]
							, "Default"_o= _o[]
							, "Description"_o= "The parameters to forward."
						}
					}
				}
				, [this] (NEncoding::CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine) -> TCFuture<uint32>
				{
					co_await fp_WaitForAppStartup();

					co_await mp_pManager
						(
							&CWebAppManagerActor::f_LaunchAsApp
							, _pCommandLine
							, _Params["Application"].f_String()
							, _Params["Executable"].f_String()
							, _Params["Params"].f_StringArray()
							, _Params["WorkingDirectory"].f_String()
						)
					;

					co_return 0;
				}
				, EDistributedAppCommandFlag_WaitForRemotes
			)
		;
	}
}
