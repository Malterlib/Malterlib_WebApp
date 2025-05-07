// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<uint32> CAcmeManagerActor::fp_CommandLine_DomainCreateAccountKey(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		if (!fg_IsValidHostname(Name))
			co_return Auditor.f_Exception("'{}' is not a valid domain name"_f << Name);

		if (!mp_Domains.f_FindEqual(Name))
			co_return Auditor.f_Exception("Domain '{}' does not exist"_f << Name);

		if (mp_SecretsManagerSubscription.m_Actors.f_IsEmpty())
			co_return Auditor.f_Exception("No secrets managers available");

		Auditor.f_Info("Created account key for domain '{}'"_f << Name);

		co_await fp_UpdateAllDomains(Name);

		co_return 0;
	}

	TCFuture<uint32> CAcmeManagerActor::fp_CommandLine_DomainChangeSettings(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		if (!fg_IsValidHostname(Name))
			co_return Auditor.f_Exception("'{}' is not a valid domain name"_f << Name);

		if (!mp_Domains.f_FindEqual(Name))
			co_return Auditor.f_Exception("Domain '{}' does not exist"_f << Name);

		auto &Domain = mp_Domains[Name];

		CDomainSettings Settings = Domain.m_Settings;

		{
			auto CaptureScope = co_await (g_CaptureExceptions % Auditor);
			fp_ParseSettings(_Params, Settings);
		}

		if (Domain.m_Settings == Settings)
			co_return Auditor.f_Exception("No setting changed");

		Domain.m_Settings = fg_Move(Settings);

		fp_SaveState(Domain);

		co_await (mp_State.m_StateDatabase.f_Save() % "[Change domain settings] Failed to save state" % Auditor);

		Auditor.f_Info("Changed domain settings '{}'"_f << Name);

		co_await fp_UpdateAllDomains(Name);

		co_return 0;
	}
}
