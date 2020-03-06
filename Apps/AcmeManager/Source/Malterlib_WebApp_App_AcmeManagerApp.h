// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Concurrency/DistributedApp>

namespace NMib::NWebApp
{
	NConcurrency::TCActor<NConcurrency::CDistributedAppActor> fg_ConstructApp_AcmeManager();
}
