// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Process/ProcessLaunchActor>

namespace NMib::NMeteor::NMeteorManager
{
	struct CUser
	{
		CUser(CStr const &_Name);

		CStr m_Name;
		CStr m_GroupID;
		CStr m_UserID;
	};

	struct CVersion
	{
		CVersion(uint32 _Major, uint32 _Minor, uint32 _Revision);
		CVersion();
		
		bool operator < (CVersion const &_Right) const;

		template <typename tf_CFormatInto>
		void f_Format(tf_CFormatInto &o_FormatInto) const;

		uint32 m_Major = 0;
		uint32 m_Minor = 0;
		uint32 m_Revision = 0;
	};
	
	struct CToolLaunch
	{
		CToolLaunch();
		~CToolLaunch();

		TCActor<CProcessLaunchActor> m_ProcessLaunch;
		TCSharedPointer<bool> m_pDestroyed;
	};
}

#include "Malterlib_Meteor_App_MeteorManager_Helpers.hpp"
