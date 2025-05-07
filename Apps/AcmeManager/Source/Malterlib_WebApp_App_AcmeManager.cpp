// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>

#ifdef DCompiler_MSVC_Workaround
#pragma warning(disable:4724)
#endif

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Concurrency/LogError>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	CAcmeManagerActor::CAcmeManagerActor()
		: CDistributedAppActor(CDistributedAppActor_Settings("AcmeManager").f_AuditCategory("Malterlib/WebApp/AcmeManager"))
	{
	}

	CAcmeManagerActor::~CAcmeManagerActor() = default;

	CEJsonSorted CAcmeManagerActor::fp_GetConfigValue(CStr const &_Name, CEJsonSorted const &_Default) const
	{
		return mp_State.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, _Default);
	}

	TCFuture<void> CAcmeManagerActor::fp_StartApp(NEncoding::CEJsonSorted const _Params)
	{
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");
					return {};
				}
			)
		;
		co_await fp_ReadState();

		CAwsCredentials AWSCredentials;

		AWSCredentials.m_AccessKeyID = fp_GetConfigValue("AWSAccessKeyID", "").f_String();
		AWSCredentials.m_SecretKey = fp_GetConfigValue("AWSSecretKey", "").f_String();

		if (AWSCredentials.m_AccessKeyID.f_IsEmpty())
			co_return DMibErrorInstance("AWSAccessKeyID value not specified in config");

		if (AWSCredentials.m_SecretKey.f_IsEmpty())
			co_return DMibErrorInstance("AWSSecretKey value not specified in config");

		mp_CurlActors.f_Construct(fg_Construct(fg_Construct(), "Curl actor"));
		mp_Route53Actor = fg_Construct(*mp_CurlActors, AWSCredentials);

		auto AccountEmailsJson = fp_GetConfigValue("ACMEAccountEmails", _[]);

		TCVector<CEJsonSorted> const &AccountEmails = AccountEmailsJson.f_Array();
		if (AccountEmails.f_IsEmpty())
			co_return DMibErrorInstance("ACMEAccountEmails value must be specified in config");

		if (!AccountEmailsJson.f_IsStringArray())
			co_return DMibErrorInstance("ACMEAccountEmails is expected to be a string array");

		mp_AcmeAccountEmails = AccountEmailsJson.f_StringArray();

		mp_SecretsManagerSubscription = co_await mp_State.m_TrustManager->f_SubscribeTrustedActors<CSecretsManager>();

		{
			auto Result = co_await mp_SecretsManagerSubscription.f_OnActor
				(
					g_ActorFunctor / [this](TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _ActorInfo) -> TCFuture<void>
					{
						co_await fp_HandleSecretsManagerAdded(_SecretsManager, _ActorInfo);

						co_return {};
					}
					, g_ActorFunctor / [this](TCWeakDistributedActor<CActor> _SecretsManager, CTrustedActorInfo _ActorInfo) -> TCFuture<void>
					{
						co_await fp_SecretsManagerRemoved(_SecretsManager, _ActorInfo).f_Wrap() > fg_LogError("Mib/WebApp/AcmeManager", "Failed to handle secrets manager removed");

						co_return {};
					}
				)
				.f_Wrap()
			;

			if (!Result)
				DMibLog(Error, "Failed when subscripbing to secrets manager: {}", Result.f_GetExceptionStr());
		}

		mp_DomainUpdateTimerSubscription = co_await fg_RegisterTimer
			(
				24.0 * 60.0 * 60.0 // 24 h
				, [this]() -> TCFuture<void>
				{
					co_await fp_UpdateAllDomains("");

					co_return {};
				}
			)
		;
		co_return {};
	}

	TCFuture<void> CAcmeManagerActor::fp_StopApp()
	{
		TCFutureVector<void> Destroys;
		if (mp_DomainUpdateTimerSubscription)
			fg_Exchange(mp_DomainUpdateTimerSubscription, nullptr)->f_Destroy() > Destroys;

		mp_SecretsManagerSubscription.f_Destroy() > Destroys;

		if (mp_Route53Actor)
			fg_Move(mp_Route53Actor).f_Destroy() > Destroys;

		mp_CurlActors.f_Destroy() > Destroys;

		for (auto &Domain : mp_Domains)
		{
			fg_Move(Domain.m_UpdateDomainSequencer).f_Destroy() > Destroys;
			if (Domain.m_DomainState)
			{
				if (Domain.m_DomainState->m_AcmeClientEC)
					fg_Move(Domain.m_DomainState->m_AcmeClientEC).f_Destroy() > Destroys;
				if (Domain.m_DomainState->m_AcmeClientRSA)
					fg_Move(Domain.m_DomainState->m_AcmeClientRSA).f_Destroy() > Destroys;

				for (auto &OnRelease : Domain.m_DomainState->m_OnReleaseDNSChallenge)
					OnRelease.f_SetException(DMibErrorInstance("Shutting down"));
				
				Domain.m_DomainState->m_OnReleaseDNSChallenge.f_Clear();
			}

		}

		co_await fg_AllDoneWrapped(Destroys);

		co_return {};
	}

	void CAcmeManagerActor::fp_UpdateDomainStatus(CDomain &o_Domain, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status)
	{
		DLogWithCategory(Mib/WebApp/AcmeManager, Info, "<{}> Changing domain status: {}", o_Domain.f_GetName(), _Status);

		auto &Status = o_Domain.m_Statuses[_HostInfo];
		Status.m_Description = _Status;
		Status.m_Severity = _Severity;
	}

	auto CAcmeManagerActor::CDomainSettings::f_Tuple() const
	{
		return fg_TupleReferences
			(
				m_EllipticCurveType
				, m_RSASettings
				, m_bGenerateRSA
				, m_bGenerateEC
				, m_bIncludeWildcard
				, m_bManualDNSChallenge
				, m_AlternateChain
				, m_AcmeDirectory
				, m_AcmeCustomDirectory
				, m_AccountKeySettings
			)
		;
	}

	bool CAcmeManagerActor::CDomainSettings::operator == (CAcmeManagerActor::CDomainSettings const &_Right) const
	{
		return f_Tuple() == _Right.f_Tuple();
	}

	CStr CAcmeManagerActor::CDomain::f_GetSecretFolder() const
	{
		return "org.malterlib.certificate/{}"_f << f_GetName();
	}

	CAcmeManagerActor::CDomainStatus const *CAcmeManagerActor::CDomain::f_GetCurrentStatus() const
	{
		if (!m_DomainState)
			return nullptr;

		return m_Statuses.f_FindEqual(m_DomainState->m_SecretsManagerHostInfo);
	}
}

namespace NMib::NWebApp
{
	TCActor<CDistributedAppActor> fg_ConstructApp_AcmeManager()
	{
		return fg_Construct<NAcmeManager::CAcmeManagerActor>();
	}
}
