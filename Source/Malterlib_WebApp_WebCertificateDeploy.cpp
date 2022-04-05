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
			, TCActor<CSeparateThreadActor> const &_FileActor
		)
		: m_pThis(_pThis)
		, m_DistributionManager(_DistributionManager)
		, m_TrustManager(_TrustManager)
		, m_FileActor(_FileActor)
	{
		if (!m_FileActor)
		{
			m_FileActor = TCActor<CSeparateThreadActor>{fg_Construct(), "Certificate Deploy File Access"};
			m_bOwnsFileActor = true;
		}
	}

	CWebCertificateDeployActor::CWebCertificateDeployActor
		(
		 	TCActor<CActorDistributionManager> const &_DistributionManager
		 	, TCActor<CDistributedActorTrustManager> const &_TrustManager
			, TCActor<CSeparateThreadActor> const &_FileActor
		)
		: mp_pInternal(fg_Construct(this, _DistributionManager, _TrustManager, _FileActor))
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
				, "Mib/WebApp/WebCertificateDeploy"
				, "Failed to handle secrets manager added"
			)
		;

		Internal.m_SecretsManagerSubscription.f_OnRemoveActor
			(
				g_ActorFunctor / [pInternal = &Internal](TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo &&_ActorInfo) -> TCFuture<void>
				{
					co_await fg_CallSafe(pInternal, &CInternal::f_SecretsManagerRemoved, _SecretsManager, _ActorInfo);

					co_return {};
				}
				, "Mib/WebApp/WebCertificateDeploy"
				, "Failed to handle secrets manager removed"
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

		TCActorResultVector<void> Results;

		for (auto &State : Internal.m_SecretsManagerStates)
		{
			if (State.m_ChangesSubscription)
				fg_Exchange(State.m_ChangesSubscription, nullptr)->f_Destroy() > Results.f_AddResult();
		}

		if (Internal.m_TimerSubscription)
			Internal.m_TimerSubscription->f_Destroy() > Results.f_AddResult();

		Internal.m_SecretsManagerSubscription.f_Destroy() > Results.f_AddResult();
		if (Internal.m_bOwnsFileActor && Internal.m_FileActor)
			Internal.m_FileActor.f_Destroy() > Results.f_AddResult();

		co_await Results.f_GetResults().f_Wrap();

		co_return {};
	}

}
