// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	struct CWebAppManagerActor;
	struct CWebAppManagerDaemonActor : public CDistributedAppActor
	{
		CWebAppManagerDaemonActor(CWebAppManagerOptions const &_Options);
		~CWebAppManagerDaemonActor();

	private:
		TCFuture<void> fp_StartApp(NEncoding::CEJSON const &_Params) override;
		TCFuture<void> fp_StopApp() override;
		TCFuture<void> fp_PreStop() override;
		void fp_PopulateAppInterfaceRegisterInfo(CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo, NEncoding::CEJSON const &_Params) override;

		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override;

		TCActor<CWebAppManagerActor> mp_pManager;
		CWebAppManagerOptions mp_Options;
	};
}
