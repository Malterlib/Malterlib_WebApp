// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>

#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<void> CAcmeManagerActor::fp_HandleSecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info)
	{
		auto Result = co_await self(&CAcmeManagerActor::fp_SecretsManagerAdded, _SecretsManager, _Info, "").f_Wrap();

		if (Result)
		{
			mp_LastSecretsManagerError.f_Remove(_SecretsManager);

			co_return {};
		}

		auto &LastError = mp_LastSecretsManagerError[_SecretsManager];

		auto Error = Result.f_GetExceptionStr();

		if (Error != LastError)
		{
			DLogWithCategory(Mib/WebApp/AcmeManager, Error, "Failed to handle secrets manager added for '{}' (will retry every 10 seconds): {}", _Info.m_HostInfo, Error);
			LastError = Error;
			for (auto &Domain : mp_Domains)
				fp_UpdateDomainStatus(Domain, _Info.m_HostInfo, EStatusSeverity_Error, Error);
		}

		if (!mp_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
			co_return {};

		if (mp_RetryingSecretsManagers(_SecretsManager).f_WasCreated())
		{
			fg_Timeout(10.0) > [=]
				{
					mp_RetryingSecretsManagers.f_Remove(_SecretsManager);

					if (!mp_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return;

					self(&CAcmeManagerActor::fp_HandleSecretsManagerAdded, _SecretsManager, _Info) > fg_LogError("SecretsManager", "Failed to handle secrets manager added");
				}
			;
		}

		co_return {};
	}

	TCFuture<void> CAcmeManagerActor::fp_SecretsManagerAdded
		(
			TCDistributedActor<CSecretsManager> const &_SecretsManager
			, CTrustedActorInfo const &_Info
			, NStr::CStr const &_CreatePrivateKeyForDomain
		)
	{
		CSecretsManager::CEnumerateSecrets EnumerateSecrets;
		EnumerateSecrets.m_SemanticID = "org.malterlib.certificate#*";

		auto Secrets = co_await _SecretsManager.f_CallActor(&CSecretsManager::f_EnumerateSecrets)(EnumerateSecrets);

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

			Domain.m_UpdateDomainSequencer.f_RunSequenced
				(
					g_ActorFunctorWeak /
					[
						this
						, DomainName
						, DomainState = fg_Move(DomainState)
						, bCreatePrivateKey = Domain.f_GetName() == _CreatePrivateKeyForDomain
					]
					(CActorSubscription &&_Subscription) mutable -> TCFuture<void>
					{
						auto *pDomain = mp_Domains.f_FindEqual(DomainName);

						if (!pDomain)
							co_return {};

						auto &Domain = *pDomain;

						if (auto pCurrentStatus = Domain.f_GetCurrentStatus())
						{
							if (pCurrentStatus->m_Severity == EStatusSeverity_Success && DomainState.m_SecretsManager != Domain.m_DomainState->m_SecretsManager)
							{
								fp_UpdateDomainStatus(Domain, DomainState.m_SecretsManagerHostInfo, EStatusSeverity_Info, "Aborted, another secrets manager already succeeded");
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

						(void)_Subscription;

						co_return {};
					}
				)
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

		co_await (fg_Move(Results) | g_Unwrap);

		co_return {};
	}

	TCFuture<void> CAcmeManagerActor::fp_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo)
	{
		mp_LastSecretsManagerError.f_Remove(_SecretsManager);
		mp_RetryingSecretsManagers.f_Remove(_SecretsManager);
		bool bDoneSometing = false;
		for (auto &Domain : mp_Domains)
		{
			if (!Domain.m_DomainState)
				continue;
			if (Domain.m_DomainState->m_SecretsManager == _SecretsManager)
			{
				Domain.m_DomainState.f_Clear();
				fp_UpdateDomainStatus(Domain, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost active secrets manager, waiting");
				bDoneSometing = true;
			}
			else
				fp_UpdateDomainStatus(Domain, _ActorInfo.m_HostInfo, EStatusSeverity_Warning, "Lost secrets manager");
		}

		if (bDoneSometing)
			co_await self(&CAcmeManagerActor::fp_UpdateAllDomains, "");

		co_return {};
	}
}
