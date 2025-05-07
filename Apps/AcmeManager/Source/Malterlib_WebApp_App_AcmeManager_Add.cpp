// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JsonShortcuts>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<uint32> CAcmeManagerActor::fp_CommandLine_DomainAdd(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CDomainSettings Settings;

		{
			auto CaptureScope = co_await (g_CaptureExceptions % Auditor);
			fp_ParseSettings(_Params, Settings);
		}

		CStr Name = _Params["Domain"].f_String();
		bool bCreateAccountKey = _Params["CreateAccountKey"].f_Boolean();

		if (!fg_IsValidHostname(Name))
			co_return Auditor.f_Exception("'{}' is not a valid domain name"_f << Name);

		if (mp_Domains.f_FindEqual(Name))
			co_return Auditor.f_Exception("Domain '{}' already exists"_f << Name);

		auto &Domain = mp_Domains[Name];

		Domain.m_Settings = fg_Move(Settings);

		fp_SaveState(Domain);

		co_await (mp_State.m_StateDatabase.f_Save() % "[Add domain] Failed to save state" % Auditor);

		Auditor.f_Info("Added domain '{}'"_f << Name);

		co_await fp_UpdateAllDomains(bCreateAccountKey ? Name : CStr());

		co_return 0;
	}
}
