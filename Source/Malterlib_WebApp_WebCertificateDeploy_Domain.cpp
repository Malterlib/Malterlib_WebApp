// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_WebCertificateDeploy_Internal.h"

namespace NMib::NWebApp
{
	TCFuture<CActorSubscription> CWebCertificateDeployActor::f_AddDomain(CDomainSettings &&_DomainSettings)
	{
		auto &Internal = *mp_pInternal;

		auto Mapped = Internal.m_Domains(_DomainSettings.m_DomainName);

		if (!Mapped.f_WasCreated())
			co_return DMibErrorInstance("Domain already added");

		auto &Domain = *Mapped;

		Domain.m_Settings = fg_Move(_DomainSettings);

		fg_CallSafe(&Internal, &CInternal::f_UpdateDomainForAllSecretsManagers, Domain.m_Settings.m_DomainName)
			> fg_LogError("Mib/WebApp/WebCertificateDeploy", "Update domain '{}' for all secrets managers had some failures"_f << Domain.m_Settings.m_DomainName)
		;

		co_return g_ActorSubscription / [pInternal = &Internal, DomainName = Domain.m_Settings.m_DomainName]() -> TCFuture<void>
			{
				if (auto *pDomain = pInternal->m_Domains.f_FindEqual(DomainName))
				{
					co_await fg_Move(pDomain->m_UpdateDomainSequencer).f_Destroy().f_Wrap();
					pInternal->m_Domains.f_Remove(DomainName);
				}
				co_return {};
			}
		;
	}

	void CWebCertificateDeployActor::CInternal::f_UpdateDomainStatus(CDomain &o_Domain, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status)
	{
		auto &Status = o_Domain.m_Statuses[_HostInfo];
		auto PreviousStatus = Status;
		Status.m_Description = _Status;
		Status.m_Severity = _Severity;
		if (Status != PreviousStatus && o_Domain.m_Settings.m_fOnStatusChange)
			o_Domain.m_Settings.m_fOnStatusChange(_HostInfo, Status) > fg_LogError("Mib/WebApp/WebCertificateDeploy", "On status change failed");
	}

	auto CWebCertificateDeployActor::CDomainStatus::f_Tuple() const
	{
		return fg_TupleReferences(m_Severity, m_Description);
	}

	bool CWebCertificateDeployActor::CDomainStatus::operator == (CDomainStatus const &_Right) const
	{
		return f_Tuple() == _Right.f_Tuple();
	}

	CStr const &CWebCertificateDeployActor::CInternal::CDomain::f_GetName() const
	{
		return TCMap<CStr, CDomain>::fs_GetKey(*this);
	}

	CStr CWebCertificateDeployActor::CInternal::CDomain::f_GetSecretFolder() const
	{
		return "org.malterlib.certificate/{}"_f << f_GetName();
	}

	auto CWebCertificateDeployActor::CInternal::CDomain::f_GetCurrentStatus() const -> CDomainStatus const *
	{
		if (!m_DomainState)
			return nullptr;

		return m_Statuses.f_FindEqual(m_DomainState->m_SecretsManagerHostInfo);
	}
}
