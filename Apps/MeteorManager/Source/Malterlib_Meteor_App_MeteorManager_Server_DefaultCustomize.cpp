// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	TCUniquePointer<ICMeteorManagerCustomization> fg_CreateMeteorManagerCustomization()
	{
		return nullptr;
	}
}
