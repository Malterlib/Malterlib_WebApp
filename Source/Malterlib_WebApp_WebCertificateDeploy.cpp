// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_WebCertificateDeploy_Internal.h"

namespace NMib::NWebApp
{
	using namespace NTime;

	CWebCertificateDeployActor::CInternal::CInternal
		(
			CWebCertificateDeployActor *_pThis
			, TCActor<CActorDistributionManager> const &_DistributionManager
			, TCActor<CDistributedActorTrustManager> const &_TrustManager
		)
		: m_pThis(_pThis)
		, m_DistributionManager(_DistributionManager)
		, m_TrustManager(_TrustManager)
	{
	}

	CWebCertificateDeployActor::CWebCertificateDeployActor
		(
			TCActor<CActorDistributionManager> const &_DistributionManager
			, TCActor<CDistributedActorTrustManager> const &_TrustManager
		)
		: mp_pInternal(fg_Construct(this, _DistributionManager, _TrustManager))
	{
	}

	CWebCertificateDeployActor::~CWebCertificateDeployActor() = default;

	TCFuture<void> CWebCertificateDeployActor::f_Start()
	{
		auto &Internal = *mp_pInternal;

		if (Internal.m_bStarted)
			co_return DMibErrorInstance("Already started");

		Internal.m_bStarted = true;

		Internal.m_SecretsManagerSubscription = co_await Internal.m_TrustManager->f_SubscribeTrustedActors<CSecretsManager>();

		co_await Internal.m_SecretsManagerSubscription.f_OnActor
			(
				g_ActorFunctor / [pInternal = &Internal](TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo) -> TCFuture<void>
				{
					co_await fg_CallSafe(*pInternal, &CInternal::f_SecretsManagerAddedWithRetry, _SecretsManager, _ActorInfo);

					co_return {};
				}
				, g_ActorFunctor / [pInternal = &Internal](TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo &&_ActorInfo) -> TCFuture<void>
				{
					co_await fg_CallSafe(pInternal, &CInternal::f_SecretsManagerRemoved, _SecretsManager, _ActorInfo);

					co_return {};
				}
				, "Mib/WebApp/WebCertificateDeploy"
				, "Failed to calling {} for secrets manager"
			)
		;

		// Retry every hour in case permission problems etc have been fixed
		Internal.m_TimerSubscription = co_await fg_RegisterTimer
			(
				CTimeSpanConvert::fs_CreateHourSpan(1).f_GetSeconds()
				, [this]() -> TCFuture<void>
				{
					auto Result = co_await fg_CallSafe(&*mp_pInternal, &CInternal::f_UpdateAllDomainsForAllSecretsManagers).f_Wrap();
					if (!Result)
						DMibLogWithCategory(Mib/WebApp/WebCertificateDeploy, Error, "Update all domains had some failures: {}", Result.f_GetExceptionStr());

					co_return {};
				}
			)
		;

		co_return {};
	}

	TCFuture<void> CWebCertificateDeployActor::fp_Destroy()
	{
		auto &Internal = *mp_pInternal;

		CLogError LogError("Mib/WebApp/WebCertificateDeploy");

		TCActorResultVector<void> Results;

		for (auto &State : Internal.m_SecretsManagerStates)
		{
			if (State.m_ChangesSubscription)
				fg_Exchange(State.m_ChangesSubscription, nullptr)->f_Destroy() > Results.f_AddResult();
		}

		if (Internal.m_TimerSubscription)
			Internal.m_TimerSubscription->f_Destroy() > Results.f_AddResult();

		Internal.m_SecretsManagerSubscription.f_Destroy() > Results.f_AddResult();

		co_await Results.f_GetUnwrappedResults().f_Wrap() > LogError.f_Warning("Failed to destroy certificate deploy actor");

		{
			TCActorResultVector<void> Destroys;

			for (auto &Domain : Internal.m_Domains)
				fg_Move(Domain.m_UpdateDomainSequencer).f_Destroy() > Destroys.f_AddResult();

			co_await Destroys.f_GetUnwrappedResults().f_Wrap() > LogError.f_Warning("Failed to destroy sequencers");
		}

		co_return {};
	}

}
