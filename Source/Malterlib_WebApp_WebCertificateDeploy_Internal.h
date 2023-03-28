// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include "Malterlib_WebApp_WebCertificateDeploy.h"

#include <Mib/Cloud/SecretsManager>
#include <Mib/Concurrency/ActorSubscription>
#include <Mib/Concurrency/ActorSequencer>
#include <Mib/Concurrency/LogError>

namespace NMib::NWebApp
{
	using namespace NStr;
	using namespace NConcurrency;
	using namespace NContainer;
	using namespace NNetwork;
	using namespace NStorage;
	using namespace NCloud;
	using namespace NException;

	struct CWebCertificateDeployActor::CInternal : public CActorInternal
	{
		CInternal
			(
				CWebCertificateDeployActor *_pThis
				, TCActor<CActorDistributionManager> const &_DistributionManager
				, TCActor<CDistributedActorTrustManager> const &_TrustManager
				, TCActor<CSeparateThreadActor> const &_FileActor
			)
		;

		struct CDomainState
		{
			TCDistributedActor<CSecretsManager> m_SecretsManager;
			CHostInfo m_SecretsManagerHostInfo;
		};

		struct CDomain
		{
			CStr const &f_GetName() const;
			CDomainStatus const *f_GetCurrentStatus() const;
			CStr f_GetSecretFolder() const;

			CDomainSettings m_Settings;
			TCOptional<CDomainState> m_DomainState;
			TCActorSequencer<void> m_UpdateDomainSequencer;
			TCMap<CHostInfo, CDomainStatus> m_Statuses;
		};

		struct CSecretsManagerState
		{
			CActorSubscription m_ChangesSubscription;
		};

		TCFuture<void> f_SecretsManagerAddedWithRetry(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info);
		TCFuture<void> f_SecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info);
		TCFuture<void> f_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo);
		CExceptionPointer f_UpdateDomain_CheckPreconditions(CStr const &_DomainName, CDomain *&o_pDomain, CDomainState *&o_pDomainState);
		TCFuture<void> f_UpdateDomainForSecretsManager(CStr const &_DomainName, TCDistributedActor<CSecretsManager> const &_SecretsManager, CHostInfo const &_SecretsManagerHostInfo);
		TCFuture<void> f_UpdateDomainForAllSecretsManagers(CStr const &_DomainName);
		TCFuture<void> f_UpdateAllDomainsForAllSecretsManagers();
		CExceptionPointer f_UpdateDomain_CheckSecret(CSecretsManager::CSecretProperties const &_Properties, CSecretsManager::CSecretID const &_SecretID, bool _bCertificate);
		TCFuture<void> f_UpdateDomain_UpdateFiles(CStr const &_DomainName, CStr const &_CertificateType, CCertificateFilesSettings const &_FileSettings);
		TCFuture<void> f_UpdateDomain(CStr const &_DomainName);
		void f_UpdateDomainStatus(CDomain &o_Domain, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status);

		CWebCertificateDeployActor *m_pThis;
		TCActor<CActorDistributionManager> m_DistributionManager;
		TCActor<CDistributedActorTrustManager> m_TrustManager;
		TCActor<CSeparateThreadActor> m_FileActor;
		TCTrustedActorSubscription<CSecretsManager> m_SecretsManagerSubscription;
		TCMap<TCWeakDistributedActor<CActor>, CStr> m_LastSecretsManagerError;
		TCSet<TCWeakDistributedActor<CActor>> m_RetryingSecretsManagers;

		CActorSubscription m_TimerSubscription;

		TCMap<TCWeakDistributedActor<CActor>, CSecretsManagerState> m_SecretsManagerStates;

		TCMap<CStr, CDomain> m_Domains;

		bool m_bStarted = false;
		bool m_bOwnsFileActor = false;
	};
}
