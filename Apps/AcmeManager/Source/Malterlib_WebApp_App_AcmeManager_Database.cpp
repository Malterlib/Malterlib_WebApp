// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	EPublicKeyType CAcmeManagerActor::fsp_EllipticCurveTypeFromStr(CStr const &_String)
	{
		if (_String == "secp256r1")
			return EPublicKeyType_EC_secp256r1;
		else if (_String == "secp384r1")
			return EPublicKeyType_EC_secp384r1;
		else if (_String == "secp521r1")
			return EPublicKeyType_EC_secp521r1;
		else if (_String == "X25519")
			return EPublicKeyType_EC_X25519;
		else
			DMibError("Unknown elliptic key type: {}"_f << _String);
	}

	CStr CAcmeManagerActor::fsp_EllipticCurveTypeToStr(EPublicKeyType _Type)
	{
		switch (_Type)
		{
		case EPublicKeyType_EC_secp256r1: return "secp256r1";
		case EPublicKeyType_EC_secp384r1: return "secp384r1";
		case EPublicKeyType_EC_secp521r1: return "secp521r1";
		case EPublicKeyType_EC_X25519: return "X25519";
		default: break;
		}
		return "Unknown";
	}

	void CAcmeManagerActor::fp_ParseSettings(CEJSON const &_Params, CDomainSettings &o_Settings)
	{
		if (auto pValue = _Params.f_GetMember("GenerateRSA", EJSONType_Boolean))
			o_Settings.m_bGenerateRSA = pValue->f_Boolean();

		if (auto pValue = _Params.f_GetMember("GenerateEC", EJSONType_Boolean))
			o_Settings.m_bGenerateEC = pValue->f_Boolean();

		if (auto pValue = _Params.f_GetMember("ManualDNSChallenge", EJSONType_Boolean))
			o_Settings.m_bManualDNSChallenge = pValue->f_Boolean();

		if (auto pValue = _Params.f_GetMember("IncludeWildcard", EJSONType_Boolean))
			o_Settings.m_bIncludeWildcard = pValue->f_Boolean();

		if (auto pValue = _Params.f_GetMember("EllipticCurveType", EJSONType_String))
			o_Settings.m_EllipticCurveType = fsp_EllipticCurveTypeFromStr(pValue->f_String());

		if (auto pValue = _Params.f_GetMember("RSAKeyLength", EJSONType_Integer))
			o_Settings.m_RSASettings.m_KeyLength = pValue->f_Integer();

		if (auto pValue = _Params.f_GetMember("AcmeDirectory", EJSONType_String))
		{
			if (pValue->f_String() == "LetsEncrypt")
			{
				o_Settings.m_AcmeDirectory = CAcmeClientActor::EDefaultDirectory_LetsEncrypt;
				o_Settings.m_AcmeCustomDirectory.f_Clear();
			}
			else if (pValue->f_String() == "LetsEncryptStaging")
			{
				o_Settings.m_AcmeDirectory = CAcmeClientActor::EDefaultDirectory_LetsEncryptStaging;
				o_Settings.m_AcmeCustomDirectory.f_Clear();
			}
			else
			{
				o_Settings.m_AcmeDirectory = CAcmeClientActor::EDefaultDirectory_Custom;
				o_Settings.m_AcmeCustomDirectory = pValue->f_String();
			}
		}

		if (auto pValue = _Params.f_GetMember("AccountKeySettings"))
		{
			if (pValue->f_IsInteger())
				o_Settings.m_AccountKeySettings = CPublicKeySettings_RSA{uint32(pValue->f_Integer())};
			else if (pValue->f_IsString())
			{
				if (pValue->f_String() == "secp521r1")
					o_Settings.m_AccountKeySettings = CPublicKeySettings_EC_secp521r1{};
				else if (pValue->f_String() == "secp384r1")
					o_Settings.m_AccountKeySettings = CPublicKeySettings_EC_secp384r1{};
				else if (pValue->f_String() == "secp256r1")
					o_Settings.m_AccountKeySettings = CPublicKeySettings_EC_secp256r1{};
				else if (pValue->f_String() == "default")
				{
					if (o_Settings.m_AcmeDirectory == CAcmeClientActor::EDefaultDirectory_LetsEncryptStaging || o_Settings.m_AcmeDirectory == CAcmeClientActor::EDefaultDirectory_LetsEncrypt)
						o_Settings.m_AccountKeySettings = CPublicKeySettings_EC_secp384r1{};
				}
			}
		}
	}

	CEJSON CAcmeManagerActor::fp_SaveSettings(CDomainSettings const &_Settings)
	{
		CEJSON Domain;

		Domain["GenerateRSA"] = _Settings.m_bGenerateRSA;
		Domain["GenerateEC"] = _Settings.m_bGenerateEC;
		Domain["IncludeWildcard"] = _Settings.m_bIncludeWildcard;
		Domain["ManualDNSChallenge"] = _Settings.m_bManualDNSChallenge;
		Domain["EllipticCurveType"] = fsp_EllipticCurveTypeToStr(_Settings.m_EllipticCurveType);
		Domain["RSAKeyLength"] = _Settings.m_RSASettings.m_KeyLength;
		switch (_Settings.m_AcmeDirectory)
		{
		case CAcmeClientActor::EDefaultDirectory_Custom:
			Domain["AcmeDirectory"] = _Settings.m_AcmeCustomDirectory;
			break;
		case CAcmeClientActor::EDefaultDirectory_LetsEncrypt:
			Domain["AcmeDirectory"] = "LetsEncrypt";
			break;
		case CAcmeClientActor::EDefaultDirectory_LetsEncryptStaging:
			Domain["AcmeDirectory"] = "LetsEncryptStaging";
			break;
		}

		switch (_Settings.m_AccountKeySettings.f_GetTypeID())
		{
		case EPublicKeyType_EC_secp256r1: Domain["AccountKeySettings"] = "secp256r1"; break;
		case EPublicKeyType_EC_secp384r1: Domain["AccountKeySettings"] = "secp384r1"; break;
		case EPublicKeyType_EC_secp521r1: Domain["AccountKeySettings"] = "secp521r1"; break;
		case EPublicKeyType_RSA: Domain["AccountKeySettings"] = _Settings.m_AccountKeySettings.f_Get<EPublicKeyType_RSA>().m_KeyLength;
		default: break;
		}

		return Domain;
	}

	void CAcmeManagerActor::fp_SaveState(CDomain const &_Domain)
	{
		auto &Domains = mp_State.m_StateDatabase.m_Data["Domains"];

		Domains.f_RemoveMember(_Domain.f_GetName());
		auto &Domain = Domains[_Domain.f_GetName()];
		Domain["Settings"] = fp_SaveSettings(_Domain.m_Settings);
	}

	TCFuture<void> CAcmeManagerActor::fp_ReadState()
	{
		return TCFuture<void>::fs_RunProtected() / [&]
			{
				auto pDomains = mp_State.m_StateDatabase.m_Data.f_GetMember("Domains");
				if (!pDomains)
					return;

				for (auto &DomainObject : pDomains->f_Object())
				{
					auto &Name = DomainObject.f_Name();
					if (!fg_IsValidHostname(Name))
						DMibError("'{}' is not a valid Domain name"_f << Name);

					auto &DomainJSON = DomainObject.f_Value();

					CDomainSettings Settings;
					fp_ParseSettings(DomainJSON["Settings"], Settings);

					auto &Domain = mp_Domains[Name];
					Domain.m_Settings = fg_Move(Settings);
				}
			}
		;
	}
}
