// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedApp>
#include <Mib/Daemon/Daemon>

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	struct CMeteorManagerActor;
	struct CMeteorManagerDaemonActor : public CDistributedAppActor
	{
		CMeteorManagerDaemonActor(CMeteorManagerOptions const &_Options);
		~CMeteorManagerDaemonActor();
		
	private:
		TCFuture<void> fp_StartApp(NEncoding::CEJSON const &_Params) override;
		TCFuture<void> fp_StopApp() override;
		TCFuture<void> fp_PreStop() override;
		void fp_PopulateAppInterfaceRegisterInfo(CDistributedAppInterfaceServer::CRegisterInfo &o_RegisterInfo, NEncoding::CEJSON const &_Params) override;
		
		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override; 
		
		TCActor<CMeteorManagerActor> mp_pManager;
		CMeteorManagerOptions mp_Options;
	};
}
