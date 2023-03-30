// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	TCFuture<uint32> CWebCertificateManagerActor::fp_CommandLine_DomainAdd(CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (f_IsDestroyed())
						return DMibErrorInstance("Shutting down");
					return {};
				}
			)
		;

		CDomainSettings Settings;

		{
			auto CaptureScope = co_await (g_CaptureExceptions % Auditor);
			fp_ParseCommandLineSettings(_Params, Settings);
		}

		if (!fg_IsValidHostname(Name))
			co_return Auditor.f_Exception("'{}' is not a valid domain name"_f << Name);

		if (mp_Domains.f_FindEqual(Name))
			co_return Auditor.f_Exception("Domain '{}' already exists"_f << Name);

		auto &Domain = mp_Domains[Name];

		Domain.m_Settings = fg_Move(Settings);

		fp_SaveState(Domain);

		co_await (mp_State.m_StateDatabase.f_Save() % "[Add domain] Failed to save state" % Auditor);

		Auditor.f_Info("Added domain '{}'"_f << Name);

		co_await self(&CWebCertificateManagerActor::fp_UpdateDomainSettings, Name);

		co_return 0;
	}
}
