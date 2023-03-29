// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_WebCertificateDeploy_Internal.h"

namespace NMib::NWebApp
{
	TCFuture<void> CWebCertificateDeployActor::CInternal::f_SecretsManagerAddedWithRetry(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		auto Result = co_await fg_CallSafe(this, &CInternal::f_SecretsManagerAdded, _SecretsManager, _Info).f_Wrap();

		if (Result)
		{
			m_LastSecretsManagerError.f_Remove(_SecretsManager);
			co_return {};
		}

		auto &LastError = m_LastSecretsManagerError[_SecretsManager];

		auto Error = Result.f_GetExceptionStr();

		if (Error != LastError)
		{
			DMibLogWithCategory
				(
					Mib/WebApp/WebCertificateDeploy
					, Error
					, "Failed to handle secrets manager added for '{}' (will retry every 10 seconds): {}"
					, _Info.m_HostInfo
					, Error
				)
			;
			LastError = Error;
			for (auto &Domain : m_Domains)
				f_UpdateDomainStatus(Domain, _Info.m_HostInfo, EStatusSeverity_Error, Error);
		}

		if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
			co_return {};

		if (m_RetryingSecretsManagers(_SecretsManager).f_WasCreated())
		{
			fg_Timeout(10.0) > [=]
				{
					m_RetryingSecretsManagers.f_Remove(_SecretsManager);

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return;

					fg_CallSafe(this, &CInternal::f_SecretsManagerAddedWithRetry, _SecretsManager, _Info)
						> fg_LogError("Mib/WebApp/WebCertificateDeploy", "Failed to handle secret manager added (retry)")
					;
				}
			;
		}

		co_return {};
	}


	TCFuture<void> CWebCertificateDeployActor::CInternal::f_SecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		TCActorResultMap<CStr, void> UpdateResults;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (m_pThis->f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return DMibErrorInstance("Secrets manager removed");

					return {};
				}
			)
		;

		CSecretsManager::CSubscribeToChanges SubscribeToChanges;
		SubscribeToChanges.m_SemanticID = "org.malterlib.certificate#*";
		SubscribeToChanges.m_fOnChanges = g_ActorFunctor / [this, _SecretsManager, _Info](CSecretsManager::CSecretChanges &&_Changes) mutable -> TCFuture<void>
			{
				if (m_pThis->f_IsDestroyed())
					co_return {};

				if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
					co_return {};

				TCSet<CStr> DomainsToUpdate;
				for (auto &Change : _Changes.m_Changed)
				{
					if (!Change.m_SemanticID)
						continue;

					auto CertificateSemanticPrefix = "org.malterlib.certificate#";

					if (!Change.m_SemanticID->f_StartsWith(CertificateSemanticPrefix))
					{
						DMibLogWithCategory(Mib/WebApp/WebCertificateDeploy, Warning, "Invalid semantic ID received from secrets manager '{}': {}", _Info.m_HostInfo, *Change.m_SemanticID);
						continue;
					}

					CStr DomainName = Change.m_SemanticID->f_RemovePrefix(CertificateSemanticPrefix);

					if (!fg_IsValidHostname(DomainName))
					{
						DMibLogWithCategory(Mib/WebApp/WebCertificateDeploy, Warning, "Invalid domain in semantic ID from secrets manager '{}': {}", _Info.m_HostInfo, DomainName);
						continue;
					}

					if (!m_Domains.f_FindEqual(DomainName))
					{
						DMibLogWithCategory
							(
								Mib/WebApp/WebCertificateDeploy
								, Warning
								, "Certificate deploy manager has access to a domain '{}' certificate that it shouldn't have access to. Secrets manager: {}"
								, DomainName
								, _Info.m_HostInfo
							)
						;
						continue;
					}

					DomainsToUpdate[DomainName];
				}

				for (auto &DomainName : DomainsToUpdate)
				{
					fg_CallSafe(this, &CInternal::f_UpdateDomainForSecretsManager, DomainName, _SecretsManager, _Info.m_HostInfo)
						> fg_LogError("Mib/WebApp/WebCertificateDeploy", "Update domain '{}' for secrets manager '{}' failed"_f << DomainName << _Info.m_HostInfo)
					;
				}

				co_return {};
			}
		;

		auto ChangesSubscription = co_await (_SecretsManager.f_CallActor(&CSecretsManager::f_SubscribeToChanges)(fg_Move(SubscribeToChanges)) % "Subscribe to secret changes");

		m_SecretsManagerStates[_SecretsManager].m_ChangesSubscription = fg_Move(ChangesSubscription);

		co_return {};
	}

	TCFuture<void> CWebCertificateDeployActor::CInternal::f_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo)
	{
		m_LastSecretsManagerError.f_Remove(_SecretsManager);
		m_RetryingSecretsManagers.f_Remove(_SecretsManager);

		auto *pSecretsManagerState = m_SecretsManagerStates.f_FindEqual(_SecretsManager);

		if (pSecretsManagerState && pSecretsManagerState->m_ChangesSubscription)
		{
			auto Subscription = fg_Exchange(pSecretsManagerState->m_ChangesSubscription, nullptr);

			m_SecretsManagerStates.f_Remove(pSecretsManagerState);

			co_await Subscription->f_Destroy().f_Wrap();
		}

		TCActorResultVector<void> UpdateDomainResults;
		for (auto &Domain : m_Domains)
		{
			if (!Domain.m_DomainState)
				continue;

			if (Domain.m_DomainState->m_SecretsManager == _SecretsManager)
			{
				Domain.m_DomainState.f_Clear();

				f_UpdateDomainStatus(Domain, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost active secrets manager, waiting");

				fg_CallSafe(this, &CInternal::f_UpdateDomainForAllSecretsManagers, Domain.f_GetName()) > UpdateDomainResults.f_AddResult();
			}
			else
				f_UpdateDomainStatus(Domain, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost secrets manager");
		}

		co_await (co_await UpdateDomainResults.f_GetResults() | g_Unwrap);

		co_return {};
	}
}
