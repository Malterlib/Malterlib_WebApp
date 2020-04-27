// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Platform>

#ifdef DCompiler_MSVC_Workaround
#pragma warning(disable:4724)
#endif

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	CAcmeManagerActor::CAcmeManagerActor()
		: CDistributedAppActor(CDistributedAppActor_Settings("AcmeManager").f_AuditCategory("Malterlib/WebApp/AcmeManager"))
	{
	}

	CAcmeManagerActor::~CAcmeManagerActor() = default;

	CEJSON CAcmeManagerActor::fp_GetConfigValue(CStr const &_Name, CEJSON const &_Default) const
	{
		return mp_State.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, _Default);
	}

	TCFuture<void> CAcmeManagerActor::fp_StartApp(NEncoding::CEJSON const &_Params)
	{
		auto OnResume = g_OnResume / [&]
			{
				if (mp_State.m_bStoppingApp || f_IsDestroyed())
					DMibError("Startup aborted");
			}
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

		auto AccountEmailsJSON = fp_GetConfigValue("ACMEAccountEmails", _[_]);

		TCVector<CEJSON> const &AccountEmails = AccountEmailsJSON.f_Array();
		if (AccountEmails.f_IsEmpty())
 			co_return DMibErrorInstance("ACMEAccountEmails value must be specified in config");

		if (!AccountEmailsJSON.f_IsStringArray())
 			co_return DMibErrorInstance("ACMEAccountEmails is expected to be a string array");

		mp_AcmeAccountEmails = AccountEmailsJSON.f_StringArray();

		mp_SecretsManagerSubscription = co_await mp_State.m_TrustManager
			(
				&CDistributedActorTrustManager::f_SubscribeTrustedActors<CSecretsManager>
				, CSecretsManager::mc_pDefaultNamespace
				, fg_ThisActor(this)
			)
		;

		mp_SecretsManagerSubscription.f_OnActor
			(
				[this](TCDistributedActor<CSecretsManager> const &_SecretsManager, CTrustedActorInfo const &_ActorInfo)
				{
					fp_HandleSecretsManagerAdded(_SecretsManager, _ActorInfo);
				}
			)
		;
		mp_SecretsManagerSubscription.f_OnRemoveActor
			(
				[this](TCWeakDistributedActor<CActor> const &_SecretsManager, CTrustedActorInfo &&_ActorInfo)
				{
					self(&CAcmeManagerActor::fp_SecretsManagerRemoved, _SecretsManager, _ActorInfo) > fg_LogError("Mib/WebApp/AcmeManager", "Failed to handle secrets manager removed");
				}
			)
		;

		mp_DomainUpdateTimerSubscription = co_await fg_RegisterTimer
			(
				24.0 * 60.0 * 60.0 // 24 h
				, [this]() -> TCFuture<void>
				{
					co_await self(&CAcmeManagerActor::fp_UpdateAllDomains, "");

					co_return {};
				}
			)
		;
		co_return {};
	}

	TCFuture<void> CAcmeManagerActor::fp_StopApp()
	{
		TCActorResultVector<void> Destroys;
		if (mp_DomainUpdateTimerSubscription)
			mp_DomainUpdateTimerSubscription->f_Destroy() > Destroys.f_AddResult();

		mp_SecretsManagerSubscription.f_Destroy() > Destroys.f_AddResult();

		if (mp_Route53Actor)
			mp_Route53Actor.f_Destroy() > Destroys.f_AddResult();

 		mp_CurlActors.f_Destroy() > Destroys.f_AddResult();

		for (auto &Domain : mp_Domains)
		{
			Domain.m_UpdateDomainSequencer.f_Abort() > Destroys.f_AddResult();
			if (Domain.m_DomainState)
			{
				if (Domain.m_DomainState->m_AcmeClientEC)
					Domain.m_DomainState->m_AcmeClientEC.f_Destroy() > Destroys.f_AddResult();
				if (Domain.m_DomainState->m_AcmeClientRSA)
					Domain.m_DomainState->m_AcmeClientRSA.f_Destroy() > Destroys.f_AddResult();
			}

		}

		co_await Destroys.f_GetResults();

		co_return {};
	}

	void CAcmeManagerActor::fp_UpdateDomainStatus(CDomain &o_Domain, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status)
	{
		auto &Status = o_Domain.m_Statuses[_HostInfo];
		Status.m_Description = _Status;
		Status.m_Severity = _Severity;
	}

	auto CAcmeManagerActor::CDomainSettings::f_Tuple() const
	{
		return fg_TupleReferences(m_EllipticCurveType, m_RSASettings, m_bGenerateRSA, m_bGenerateEC, m_bIncludeWildcard, m_AcmeDirectory, m_AcmeCustomDirectory, m_AccountKeySettings);
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
