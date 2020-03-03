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

		void f_HandleSecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info);
		TCFuture<void> f_SecretsManagerAdded(TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_Info);
		TCFuture<void> f_SecretsManagerRemoved(TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo);
		void f_UpdateDomain_CheckPreconditions(CStr const &_DomainName, CDomain *&o_pDomain, CDomainState *&o_pDomainState);
		TCFuture<void> f_UpdateDomainForSecretManager(CStr const &_DomainName, TCDistributedActor<CSecretsManager> const &_SecretsManager, CHostInfo const &_SecretsManagerHostInfo);
		TCFuture<void> f_UpdateDomainForAllSecretManagers(CStr const &_DomainName);
		TCFuture<void> f_UpdateDomain(CStr const &_DomainName);
		TCFuture<void> f_UpdateAllDomains();
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
