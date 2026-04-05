// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	TCSharedPointer<ICWebAppManagerCustomization> fg_CreateWebAppManagerCustomization()
	{
		return nullptr;
	}
}
