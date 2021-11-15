// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>

#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	namespace
	{
		struct CAttributeMap
		{
			ch8 const *m_pName = nullptr;
			EFileAttrib m_Attribute = EFileAttrib_None;
		};

		constexpr CAttributeMap gc_AttributeNames[] =
			{
				{"UserExecute", EFileAttrib_UserExecute}
				, {"UserRead", EFileAttrib_UserRead}
				, {"UserWrite", EFileAttrib_UserWrite}
				, {"GroupExecute", EFileAttrib_GroupExecute}
				, {"GroupRead", EFileAttrib_GroupRead}
				, {"GroupWrite", EFileAttrib_GroupWrite}
				, {"EveryoneExecute", EFileAttrib_EveryoneExecute}
				, {"EveryoneRead", EFileAttrib_EveryoneRead}
				, {"EveryoneWrite", EFileAttrib_EveryoneWrite}
			}
		;
	}
	EFileAttrib CWebCertificateManagerActor::fsp_ParseAttributes(CEJSON const &_JSON, EFileAttrib _OriginalAttribs)
	{
		if (!_JSON.f_IsStringArray())
			return _OriginalAttribs;

		EFileAttrib Attributes = EFileAttrib_None;

		for (auto &Attribute : _JSON.f_Array())
		{
			bool bFound = false;
			for (auto &Name : gc_AttributeNames)
			{
				if (Attribute.f_String() == Name.m_pName)
				{
					Attributes |= Name.m_Attribute;
					bFound = true;
					break;
				}
			}

			if (!bFound)
				DMibError("Unknown attribute: {}"_f << Attribute.f_String());
		}

		return Attributes;
	}

	CEJSON CWebCertificateManagerActor::fsp_GenerateAttributes(EFileAttrib _Attributes)
	{
		CEJSON Return = EJSONType_Array;

		for (auto &Name : gc_AttributeNames)
		{
			if (_Attributes & Name.m_Attribute)
				Return.f_Insert(Name.m_pName);
		}

		return Return;
	}

	void CWebCertificateManagerActor::fp_ParseSettings(CEJSON const &_Params, CDomainSettings &o_Settings)
	{
		auto fParseLocation = [&](CEJSON const &_JSON) -> CCertificateLocation
			{
				CCertificateLocation Location;

				if (auto pValue = _JSON.f_GetMember("Key", EJSONType_String))
					Location.m_Key = pValue->f_String();

				if (auto pValue = _JSON.f_GetMember("FullChain", EJSONType_String))
					Location.m_FullChain = pValue->f_String();

				return Location;
			}
		;

		if (auto pValue = _Params.f_GetMember("LocationEc", EJSONType_Null))
			o_Settings.m_Location_Ec.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationEc", EJSONType_Object))
			o_Settings.m_Location_Ec = fParseLocation(*pValue);

		if (auto pValue = _Params.f_GetMember("LocationRsa", EJSONType_Null))
			o_Settings.m_Location_Rsa.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationRsa", EJSONType_Object))
			o_Settings.m_Location_Rsa = fParseLocation(*pValue);

		if (auto pValue = _Params.f_GetMember("LocationNginxPid", EJSONType_Null))
			o_Settings.m_Location_NginxPid.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationNginxPid", EJSONType_String))
			o_Settings.m_Location_NginxPid = pValue->f_String();

		auto fParseFileSettings = [&](CEJSON const &_JSON) -> CCertificateFileSettings
			{
				CCertificateFileSettings FileSettings;

				if (auto pValue = _JSON.f_GetMember("User", EJSONType_String))
					FileSettings.m_User = pValue->f_String();

				if (auto pValue = _JSON.f_GetMember("Group", EJSONType_String))
					FileSettings.m_Group = pValue->f_String();

				if (auto pValue = _JSON.f_GetMember("Attributes", EJSONType_Array))
					FileSettings.m_Attributes = fsp_ParseAttributes(*pValue, FileSettings.m_Attributes);

				return FileSettings;
			}
		;

		if (auto pValue = _Params.f_GetMember("FileSettingsCertificate", EJSONType_Object))
			o_Settings.m_FileSettings_Certificate = fParseFileSettings(*pValue);
		if (auto pValue = _Params.f_GetMember("FileSettingsKey", EJSONType_Object))
			o_Settings.m_FileSettings_Key = fParseFileSettings(*pValue);
	}

	CEJSON CWebCertificateManagerActor::fp_SaveSettings(CDomainSettings const &_Settings)
	{
		CEJSON Domain;

		auto fGenerateLocation = [&](CCertificateLocation const &_Location) -> CEJSON
			{
				CEJSON Location;

				Location["Key"] = _Location.m_Key;
				Location["FullChain"] = _Location.m_FullChain;

				return Location;
			}
		;

		if (_Settings.m_Location_Ec)
			Domain["LocationEc"] = fGenerateLocation(*_Settings.m_Location_Ec);
		else
			Domain["LocationEc"] = nullptr;

		if (_Settings.m_Location_Rsa)
			Domain["LocationRsa"] = fGenerateLocation(*_Settings.m_Location_Rsa);
		else
			Domain["LocationRsa"] = nullptr;

		if (_Settings.m_Location_NginxPid)
			Domain["LocationNginxPid"] = *_Settings.m_Location_NginxPid;
		else
			Domain["LocationNginxPid"] = nullptr;

		auto fGenerateFileSettings = [&](CCertificateFileSettings const &_FileSettings) -> CEJSON
			{
				CEJSON FileSettings;

				FileSettings["User"] = _FileSettings.m_User;
				FileSettings["Group"] = _FileSettings.m_Group;
				FileSettings["Attributes"] = fsp_GenerateAttributes(_FileSettings.m_Attributes);

				return FileSettings;
			}
		;

		Domain["FileSettingsCertificate"] = fGenerateFileSettings(_Settings.m_FileSettings_Certificate);
		Domain["FileSettingsKey"] = fGenerateFileSettings(_Settings.m_FileSettings_Key);

		return Domain;
	}

	void CWebCertificateManagerActor::fp_SaveState(CDomain const &_Domain)
	{
		auto &Domains = mp_State.m_StateDatabase.m_Data["Domains"];

		Domains.f_RemoveMember(_Domain.f_GetName());
		auto &Domain = Domains[_Domain.f_GetName()];
		Domain["Settings"] = fp_SaveSettings(_Domain.m_Settings);
	}

	TCFuture<void> CWebCertificateManagerActor::fp_ReadState()
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

				for (auto &Domain : mp_Domains)
					self(&CWebCertificateManagerActor::fp_UpdateDomainSettings, Domain.f_GetName()) > fg_LogError("Mib/WebApp/WebCertificateManager", "Failed to update domain settings");
			}
		;
	}
}
