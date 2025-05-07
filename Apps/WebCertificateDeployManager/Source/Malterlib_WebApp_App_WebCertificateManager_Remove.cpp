// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Cryptography/RandomID>
#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	TCFuture<uint32> CWebCertificateManagerActor::fp_CommandLine_DomainRemove(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		auto *pDomain = mp_Domains.f_FindEqual(Name);
		if (!pDomain)
			co_return Auditor.f_Exception(fg_Format("No such domain '{}'", Name));

		uint32 Return = 0;
		if (pDomain->m_CertificateDeploySubscription)
		{
			auto Result = co_await fg_Exchange(pDomain->m_CertificateDeploySubscription, nullptr)->f_Destroy().f_Wrap();
			if (!Result)
			{
				*_pCommandLine %= "Error removing deploy subscription: {}\n"_f << Result.f_GetExceptionStr();
				Return = 1;
			}
		}

		mp_Domains.f_Remove(Name);

		if (auto *pDomainState = mp_State.m_StateDatabase.m_Data.f_GetMember("Domains"))
		{
			if (pDomainState->f_GetMember(Name))
				pDomainState->f_RemoveMember(Name);
		}

		co_await (mp_State.m_StateDatabase.f_Save() % "[Remove domain] Failed to save state" % Auditor);

		Auditor.f_Info(fg_Format("Removed domain '{}'", Name));

		co_return Return;
	}
}
