// Copyright © 2018 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<void> CAcmeManagerActor::fp_SecretsManagerAdded
		(
			TCDistributedActor<CSecretsManager> const &_SecretsManager
			, CTrustedActorInfo const &_Info
			, NStr::CStr const &_CreatePrivateKeyForDomain
		)
	{
		auto Secrets = co_await _SecretsManager.f_CallActor(&CSecretsManager::f_EnumerateSecrets)(CStrSecure("org.malterlib.certificate#*"), TCSet<CStrSecure>{});

		TCActorResultMap<CStr, void> UpdateResults;

		for (auto &Domain : mp_Domains)
		{
			CStr const &DomainName = mp_Domains.fs_GetKey(Domain);
			if (_CreatePrivateKeyForDomain && Domain.f_GetName() != _CreatePrivateKeyForDomain)
				continue;

			CSecretsManager::CSecretID SecretID;
			SecretID.m_Folder = Domain.f_GetSecretFolder() / "ACME";
			SecretID.m_Name = "AccountPrivateKey";

			if (!Secrets.f_FindEqual(SecretID) && Domain.f_GetName() != _CreatePrivateKeyForDomain)
			{
				fp_UpdateDomainStatus(Domain, _Info.m_HostInfo, EStatusSeverity_Error, "Secrets manager does not contain the account private key");
				continue;
			}

			CDomainState DomainState;
			DomainState.m_SecretsManager = _SecretsManager;
			DomainState.m_SecretsManagerHostInfo = _Info.m_HostInfo;

			Domain.m_UpdateDomainSequencer /
				[
					this
					, DomainName
					, DomainState = fg_Move(DomainState)
					, bCreatePrivateKey = Domain.f_GetName() == _CreatePrivateKeyForDomain
				]
				() mutable -> TCFuture<void>
				{
					auto *pDomain = mp_Domains.f_FindEqual(DomainName);

					if (!pDomain)
						co_return {};

					auto &Domain = *pDomain;

					if (auto pCurrentStatus = Domain.f_GetCurrentStatus())
					{
						if (pCurrentStatus->m_Severity == EStatusSeverity_Success && DomainState.m_SecretsManager != Domain.m_DomainState->m_SecretsManager)
						{
							fp_UpdateDomainStatus(Domain, DomainState.m_SecretsManagerHostInfo, EStatusSeverity_Info, "Aborted, another secret manager already succeeded");
							co_return {};
						}
					}

					Domain.m_DomainState = fg_Move(DomainState);

					co_await
						(
							self(&CAcmeManagerActor::fp_UpdateDomain, Domain.f_GetName(), bCreatePrivateKey)
							% ("Failed to update domain '{}'"_f << Domain.f_GetName())
						)
					;

					co_return {};
				}
				> UpdateResults.f_AddResult(Domain.f_GetName())
			;

		}

		auto Results = co_await UpdateResults.f_GetResults();

		for (auto &Result : Results)
		{
			if (!Result)
			{
				CStr const &DomainName = Results.fs_GetKey(Result);

				if (auto pDomain = mp_Domains.f_FindEqual(DomainName))
					fp_UpdateDomainStatus(*pDomain, _Info.m_HostInfo, EStatusSeverity_Error, "Error updating domain: {}"_f << Result.f_GetExceptionStr());
			}
		}

		fg_Move(Results) | g_Unwrap;

		co_return {};
	}

	TCFuture<void> CAcmeManagerActor::fp_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo)
	{
		bool bDoneSometing = false;
		for (auto &Domain : mp_Domains)
		{
			if (!Domain.m_DomainState)
				continue;
			if (Domain.m_DomainState->m_SecretsManager == _SecretsManager)
			{
				Domain.m_DomainState.f_Clear();
				fp_UpdateDomainStatus(Domain, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost secrets manager, waiting");
				bDoneSometing = true;
			}
		}

		if (bDoneSometing)
			co_await self(&CAcmeManagerActor::fp_UpdateAllDomains, "");

		co_return {};
	}
}
