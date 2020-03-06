// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<void> CAcmeManagerActor::fp_UpdateAllDomains(CStr const &_CreateAccountForDomainName)
	{
		DMibLogWithCategory(Mib/WebApp/AcmeManager, Info, "Updating all domains");

		TCActorResultVector<void> Results;
		for (auto &SecretsManager : mp_SecretsManagerSubscription.m_Actors)
			self(&CAcmeManagerActor::fp_SecretsManagerAdded, SecretsManager.m_Actor, SecretsManager.m_TrustInfo, _CreateAccountForDomainName) > Results.f_AddResult();

		co_await Results.f_GetResults() | g_Unwrap;

		co_return {};
	}

	void CAcmeManagerActor::fp_UpdateDomain_CheckPreconditions(CStr const &_DomainName, CDomain *&o_pDomain, CDomainState *&o_pDomainState)
	{
		if (f_IsDestroyed())
			DMibError("Shutting down");

		o_pDomain = mp_Domains.f_FindEqual(_DomainName);
		if (!o_pDomain)
			DMibError("Domain no longer exists");

		if (!o_pDomain->m_DomainState)
			DMibError("Domain no longer connected to secrets manager");
		o_pDomainState = &*o_pDomain->m_DomainState;
	}

	TCFuture<bool> CAcmeManagerActor::fp_UpdateDomain_HandleChallenge
		(
			CStr const &_DomainName
			, CStr const &_CertificateType
			, CAcmeClientActor::CChallenge const &_Challenge
		)
	{
		CDomain *pDomain = nullptr;
		CDomainState *pDomainState = nullptr;

		auto OnResume = g_OnResume / [&]
			{
				fp_UpdateDomain_CheckPreconditions(_DomainName, pDomain, pDomainState);
			}
		;

		if (_Challenge.m_Type != CAcmeClientActor::EChallengeType_Dns01)
			co_return false;

		CStr FullyQualifiedDomain = _Challenge.m_DomainName + ".";

		CAwsRoute53Actor::CListHostedZonesByNameParams Params;
		Params.m_DNSName = FullyQualifiedDomain;
		auto Zones = co_await mp_Route53Actor(&CAwsRoute53Actor::f_ListHostedZonesByName, Params);

		CTime Now = CTime::fs_NowUTC();
		CTime ExpireDate = Now - CTimeSpanConvert::fs_CreateWeekSpan(1);

		fp_UpdateDomainStatus
			(
				*pDomain
				, pDomainState->m_SecretsManagerHostInfo
				, EStatusSeverity_Info
				, "Issuing {} certificate: Applying dns-01 challenge"_f << _CertificateType
			)
		;

		for (auto &Zone : Zones)
		{
			if (Zone.m_Name != FullyQualifiedDomain)
				continue;

			CAwsRoute53Actor::CListResourceRecordSetsParams Params;
			Params.m_Name = "_acme-challenge.{}"_f << FullyQualifiedDomain;
			Params.m_Type = CAwsRoute53Actor::EResourceRecordType_TXT;
			Params.m_MaxItems = 1;

			auto Sets = co_await mp_Route53Actor(&CAwsRoute53Actor::f_ListResourceRecordSets, Zone.m_ID, Params);

			CEJSON State = EJSONType_Object;

			CAwsRoute53Actor::CChangeResourceRecordSetsParams SetParams;
			auto &Change = SetParams.m_Changes.f_Insert();
			Change.m_RecordSet.m_Name = *Params.m_Name;
			Change.m_RecordSet.m_Type = CAwsRoute53Actor::EResourceRecordType_TXT;
			Change.m_RecordSet.m_TTL = 300;

			CStr TokenToAdd = "\"{}\""_f << _Challenge.m_Token;

			TCSet<CStr> AddedRecords;

			if (!Sets.f_IsEmpty())
			{
				auto &Set = Sets[0];

				for (auto &Record : Set.m_ResourceRecords)
				{
					if (Record == TokenToAdd)
						co_return true; // Challenge is already in DNS records

					if (Record.f_StartsWith("\"State\" "))
					{
						CStr Base64 = CStr::fs_Join(Record.f_RemovePrefix("\"State\" \"").f_RemoveSuffix("\"").f_Split("\" \""), "");
						try
						{
							State = CEJSON::fs_FromString(fg_Base64Decode(Base64));
							if (!State.f_IsObject())
								State = EJSONType_Object;
						}
						catch (CException const &)
						{
						}
					}
				}

				auto SourceDates = fg_Move(State["AddedDates"]);
				if (!SourceDates.f_IsObject())
					SourceDates = EJSONType_Object;
				auto &DestinationDates = (State["AddedDates"] = EJSONType_Object);

				for (auto &Record : Set.m_ResourceRecords)
				{
					if (Record.f_StartsWith("\"State\" "))
						continue;

					auto *pDate = SourceDates.f_GetMember(Record, EEJSONType_Date);

					if (pDate && pDate->f_Date() < ExpireDate)
						continue;

					if (!AddedRecords(Record).f_WasCreated())
						continue;

					if (pDate)
						DestinationDates[Record] = pDate->f_Date();
					else
						DestinationDates[Record] = Now;

					Change.m_RecordSet.m_ResourceRecords.f_Insert(Record);
				}
			}

			Change.m_RecordSet.m_ResourceRecords.f_Insert(TokenToAdd);
			State["AddedDates"][_Challenge.m_Token] = Now;

			constexpr mint c_MaxTxtRecordSubStringLength = 250;

			CStr EncodedRecord = "\"State\"";
			for (CStr WholeString = fg_Base64Encode(State.f_ToString(nullptr)); WholeString; WholeString = WholeString.f_Extract(c_MaxTxtRecordSubStringLength))
			{
				CStr Part = WholeString.f_Left(c_MaxTxtRecordSubStringLength);
				EncodedRecord += " \"{}\""_f << Part;
			}
			Change.m_RecordSet.m_ResourceRecords.f_Insert(EncodedRecord);

			co_await mp_Route53Actor(&CAwsRoute53Actor::f_ChangeResourceRecordSets, Zone.m_ID, SetParams);

			fp_UpdateDomainStatus
				(
					*pDomain
					, pDomainState->m_SecretsManagerHostInfo
					, EStatusSeverity_Info
					, "Issuing {} certificate: Successfully applied dns-01 challenge"_f << _CertificateType
				)
			;

			co_return true;
		}

		co_return false;
	}

	TCFuture<void> CAcmeManagerActor::fp_UpdateDomain_RunAcmeProtocol
		(
			CStr const &_DomainName
			, CStr const &_CertificateType
			, CPublicKeySetting const &_PublicKeySettings
			, TCActor<CAcmeClientActor> const &_AcmeClient
			, CDomainSettings const &_DomainSettings
			, CEJSON const &_DomainSettingsJson
		)
	{
		CDomain *pDomain = nullptr;
		CDomainState *pDomainState = nullptr;

		auto OnResume = g_OnResume / [&]
			{
				fp_UpdateDomain_CheckPreconditions(_DomainName, pDomain, pDomainState);
			}
		;

		fp_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Info, "Issuing {} certificate"_f << _CertificateType);

		CAcmeClientActor::CCertificateRequest CertificateRequest;
		CertificateRequest.m_DnsNames.f_Insert(_DomainName);
		if (_DomainSettings.m_bIncludeWildcard)
			CertificateRequest.m_DnsNames.f_Insert("*." + _DomainName);
		CertificateRequest.m_KeySettings = _PublicKeySettings;
		CertificateRequest.m_fChallenge = g_ActorFunctor / [this, _DomainName, _CertificateType](CAcmeClientActor::CChallenge const &_Challenge) -> TCFuture<bool>
			{
				co_return co_await self(&CAcmeManagerActor::fp_UpdateDomain_HandleChallenge, _DomainName, _CertificateType, _Challenge);
			}
		;

		auto Certificates = co_await _AcmeClient(&CAcmeClientActor::f_RequestCertificate, fg_Move(CertificateRequest));

		CStr CertificateDescription;
		{
			try
			{
				CertificateDescription = CCertificate::fs_GetCertificateDescription
					(
						CByteVector((uint8 const *)Certificates.m_EndEntity.f_GetStr(), Certificates.m_EndEntity.f_GetLen())
					)
				;
			}
			catch (CException const &_Exception)
			{
				co_return DMibErrorInstance("Exception getting certificate description: {}"_f << _Exception);
			}
		}

		TCActorResultVector<void> StoreResults;
		CStr CertificatesSecretFolder = pDomain->f_GetSecretFolder() / "Certificates" / _CertificateType;

		auto fStoreSecret = [&](CStr const &_Key, CStrSecure const &_Data)
			{
				CSecretsManager::CSecretProperties Properties;
				Properties.f_SetSecret(_Data);
				Properties.f_SetSemanticID(CStrSecure::CFormat("org.malterlib.certificate#{}") << _DomainName);
				Properties.f_SetMetadata("Settings", fg_TempCopy(_DomainSettingsJson));

				CSecretsManager::CSecretID SecretID;
				SecretID.m_Folder = CertificatesSecretFolder;
				SecretID.m_Name = _Key;

				pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_SetSecretProperties)(SecretID, fg_TempCopy(Properties)) > StoreResults.f_AddResult();
			}
		;

		fStoreSecret("PrivateKey", Certificates.m_PrivateKey);
		fStoreSecret("FullChain", Certificates.m_FullChain);
		fStoreSecret("EndEntity", Certificates.m_EndEntity);
		fStoreSecret("Issuer", Certificates.m_Issuer);
		fStoreSecret("Other", Certificates.m_Other);

		co_await StoreResults.f_GetResults() | g_Unwrap;

		DMibLogWithCategory
			(
				Mib/WebApp/AcmeManager
				, Info
				, "Issued {} certificate for {} now stored on '{}' at {}:\n{}"
				, _CertificateType
				, _DomainName
				, pDomainState->m_SecretsManagerHostInfo.f_GetDesc()
				, CertificatesSecretFolder
				, CertificateDescription.f_Indent("    ")
			)
		;

		co_return {};
	}

	TCFuture<bool> CAcmeManagerActor::fp_UpdateDomain_IsAlreadyUpToDate(CStr const &_DomainName, CEJSON const &_DomainSettingsJson, CStr const &_CertificateType)
	{
		CDomain *pDomain = nullptr;
		CDomainState *pDomainState = nullptr;

		auto OnResume = g_OnResume / [&]
			{
				fp_UpdateDomain_CheckPreconditions(_DomainName, pDomain, pDomainState);
			}
		;

		CSecretsManager::CSecretID SecretID;
		SecretID.m_Folder = pDomain->f_GetSecretFolder() / "Certificates" / _CertificateType;
		SecretID.m_Name = "EndEntity";

		try
		{
			auto ExistingCertificate = co_await pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_GetSecretProperties)(SecretID).f_Wrap();

			if (ExistingCertificate && ExistingCertificate->m_Secret && ExistingCertificate->m_Secret->f_IsOfType<NStr::CStrSecure>() && ExistingCertificate->m_Metadata)
			{
				auto *pStoredSettings = ExistingCertificate->m_Metadata->f_FindEqual("Settings");

				if (pStoredSettings && *pStoredSettings == _DomainSettingsJson)
				{
					auto &StringData = ExistingCertificate->m_Secret->f_GetAsType<NStr::CStrSecure>();
					CByteVector CertificateData((uint8 const *)StringData.f_GetStr(), StringData.f_GetLen());

					auto IssueTime = CCertificate::fs_GetCertificateIssueTime(CertificateData);
					auto ExpirationTime = CCertificate::fs_GetCertificateExpirationTime(CertificateData);
					auto HalfTime = IssueTime + (ExpirationTime - IssueTime) / 2;

					auto Now = CTime::fs_NowUTC();

					if (Now > IssueTime && Now < HalfTime)
						co_return true;
				}
			}
		}
		catch (CException const &)
		{
		}

		co_return false;
	}

	TCFuture<void> CAcmeManagerActor::fp_UpdateDomain(CStr const &_DomainName, bool _bCreateAccountKey)
	{
		CDomain *pDomain = nullptr;
		CDomainState *pDomainState = nullptr;

		auto OnResume = g_OnResume / [&]
			{
				fp_UpdateDomain_CheckPreconditions(_DomainName, pDomain, pDomainState);
			}
		;

		fp_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Info, "Secrets manager connected");

		auto DomainSettings = pDomain->m_Settings;
		auto DomainSettingsJson = fp_SaveSettings(DomainSettings);

		bool bRsaUpToDate = false;
		bool bEcUpToDate = false;

		if (!DomainSettings.m_bGenerateRSA || co_await self(&CAcmeManagerActor::fp_UpdateDomain_IsAlreadyUpToDate, _DomainName, DomainSettingsJson, "RSA"))
			bRsaUpToDate = true;

		if (!DomainSettings.m_bGenerateEC || co_await self(&CAcmeManagerActor::fp_UpdateDomain_IsAlreadyUpToDate, _DomainName, DomainSettingsJson, "EC"))
			bEcUpToDate = true;

		if (bEcUpToDate && bRsaUpToDate)
		{
			fp_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Success, "All certificates were already up to date");
			co_return {};
		}

		CSecretsManager::CSecretID SecretID;
		SecretID.m_Folder = pDomain->f_GetSecretFolder() / "ACME";
		SecretID.m_Name = "AccountPrivateKey";

		auto SecretPropertiesResult = co_await pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_GetSecretProperties)(SecretID).f_Wrap();

		if (!SecretPropertiesResult)
		{
			if (_bCreateAccountKey && SecretPropertiesResult.f_GetExceptionStr().f_StartsWith("No secret matching ID:"))
			{
				fp_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Info, "Creating account");

				try
				{
					NContainer::CSecureByteVector PublicKeyData;
					NContainer::CSecureByteVector PrivateKeyData;
					CPublicCrypto::fs_GenerateKeys(PrivateKeyData, PublicKeyData, DomainSettings.m_AccountKeySettings);

					CSecretsManager::CSecretProperties Properties;

					Properties.f_SetSecret(fg_Move(PrivateKeyData));
					Properties.f_SetSemanticID(CStrSecure::CFormat("org.malterlib.certificate#acme#{}") << _DomainName);

					co_await pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_SetSecretProperties)(SecretID, fg_Move(Properties));

					SecretPropertiesResult = co_await pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_GetSecretProperties)(SecretID).f_Wrap();
					if (!SecretPropertiesResult)
						co_return SecretPropertiesResult.f_GetException();
				}
				catch (NException::CException const &_Exception)
				{
					co_return _Exception.f_ExceptionPointer();
				}
			}
			else
				co_return SecretPropertiesResult.f_GetException();
		}

		auto &SecretProperties = *SecretPropertiesResult;

		if (!SecretProperties.m_Secret)
			co_return DMibErrorInstance("No secret found for '{}' on secrets manager"_f << SecretID);

		if (!SecretProperties.m_Secret->f_IsOfType<NContainer::CSecureByteVector>())
			co_return DMibErrorInstance("Expected '{}' to be a binary secret"_f << SecretID);

		pDomainState->m_ACMEAccountPrivateKey = SecretProperties.m_Secret->f_GetAsType<NContainer::CSecureByteVector>();

		auto fAcmeDependencies = [&]
			{
				CAcmeClientActor::CDependencies AcmeDependencies(DomainSettings.m_AcmeDirectory, DomainSettings.m_AcmeCustomDirectory);

				AcmeDependencies.m_CurlActor = *mp_CurlActors;
				AcmeDependencies.m_AccountInfo.m_AccountPrivateKey = pDomainState->m_ACMEAccountPrivateKey;
				AcmeDependencies.m_AccountInfo.m_Emails = mp_AcmeAccountEmails;

				return AcmeDependencies;
			}
		;

		if (DomainSettings.m_bGenerateRSA && !bRsaUpToDate)
		{
			pDomainState->m_AcmeClientRSA = fg_Construct(fAcmeDependencies());

			co_await self
				(
					&CAcmeManagerActor::fp_UpdateDomain_RunAcmeProtocol
					, _DomainName
					, "RSA"
					, CPublicKeySetting(DomainSettings.m_RSASettings)
					, pDomainState->m_AcmeClientRSA
					, DomainSettings
					, DomainSettingsJson
				)
			;
		}

		if (DomainSettings.m_bGenerateEC && !bEcUpToDate)
		{
			pDomainState->m_AcmeClientEC = fg_Construct(fAcmeDependencies());

			auto fPublicKeySettings = [&]() -> CPublicKeySetting
				{
					switch (DomainSettings.m_EllipticCurveType)
					{
					case EPublicKeyType_EC_secp256r1: return CPublicKeySettings_EC_secp256r1{};
					case EPublicKeyType_EC_secp384r1: return CPublicKeySettings_EC_secp384r1{};
					case EPublicKeyType_EC_secp521r1: return CPublicKeySettings_EC_secp521r1{};
					case EPublicKeyType_EC_X25519: return CPublicKeySettings_EC_X25519{};
					case EPublicKeyType_RSA: break;
					}
					return CPublicKeySettings_EC_secp521r1{};
				}
			;

			co_await self
				(
					&CAcmeManagerActor::fp_UpdateDomain_RunAcmeProtocol
					, _DomainName
					, "EC"
					, fPublicKeySettings()
					, pDomainState->m_AcmeClientEC
					, DomainSettings
					, DomainSettingsJson
				)
			;
		}

		fp_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Success, "All certificates issued and up to date");

		co_return {};
	}
}
