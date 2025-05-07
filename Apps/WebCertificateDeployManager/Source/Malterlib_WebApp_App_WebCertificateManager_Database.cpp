// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JsonShortcuts>
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
	EFileAttrib CWebCertificateManagerActor::fsp_ParseAttributes(CEJsonSorted const &_Json, EFileAttrib _OriginalAttribs)
	{
		if (!_Json.f_IsStringArray())
			return _OriginalAttribs;

		EFileAttrib Attributes = EFileAttrib_None;

		for (auto &Attribute : _Json.f_Array())
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

	CEJsonSorted CWebCertificateManagerActor::fsp_GenerateAttributes(EFileAttrib _Attributes)
	{
		CEJsonSorted Return = EJsonType_Array;

		for (auto &Name : gc_AttributeNames)
		{
			if (_Attributes & Name.m_Attribute)
				Return.f_Insert(Name.m_pName);
		}

		return Return;
	}

	void CWebCertificateManagerActor::fp_ParseSettings(CEJsonSorted const &_Params, CDomainSettings &o_Settings)
	{
		auto fParseLocation = [&](CEJsonSorted const &_Json) -> CCertificateLocation
			{
				CCertificateLocation Location;

				if (auto pValue = _Json.f_GetMember("Key", EJsonType_String))
					Location.m_Key = pValue->f_String();

				if (auto pValue = _Json.f_GetMember("FullChain", EJsonType_String))
					Location.m_FullChain = pValue->f_String();

				return Location;
			}
		;

		if (_Params.f_GetMember("LocationEc", EJsonType_Null))
			o_Settings.m_Location_Ec.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationEc", EJsonType_Object))
			o_Settings.m_Location_Ec = fParseLocation(*pValue);

		if (_Params.f_GetMember("LocationRsa", EJsonType_Null))
			o_Settings.m_Location_Rsa.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationRsa", EJsonType_Object))
			o_Settings.m_Location_Rsa = fParseLocation(*pValue);

		if (_Params.f_GetMember("LocationNginxPid", EJsonType_Null))
			o_Settings.m_Location_NginxPid.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationNginxPid", EJsonType_String))
			o_Settings.m_Location_NginxPid = pValue->f_String();

		auto fParseFileSettings = [&](CEJsonSorted const &_Json) -> CCertificateFileSettings
			{
				CCertificateFileSettings FileSettings;

				if (auto pValue = _Json.f_GetMember("User", EJsonType_String))
					FileSettings.m_User = pValue->f_String();

				if (auto pValue = _Json.f_GetMember("Group", EJsonType_String))
					FileSettings.m_Group = pValue->f_String();

				if (auto pValue = _Json.f_GetMember("Attributes", EJsonType_Array))
					FileSettings.m_Attributes = fsp_ParseAttributes(*pValue, FileSettings.m_Attributes);

				return FileSettings;
			}
		;

		if (auto pValue = _Params.f_GetMember("FileSettingsCertificate", EJsonType_Object))
			o_Settings.m_FileSettings_Certificate = fParseFileSettings(*pValue);
		if (auto pValue = _Params.f_GetMember("FileSettingsKey", EJsonType_Object))
			o_Settings.m_FileSettings_Key = fParseFileSettings(*pValue);
	}

	CEJsonSorted CWebCertificateManagerActor::fp_SaveSettings(CDomainSettings const &_Settings)
	{
		CEJsonSorted Domain;

		auto fGenerateLocation = [&](CCertificateLocation const &_Location) -> CEJsonSorted
			{
				CEJsonSorted Location;

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

		auto fGenerateFileSettings = [&](CCertificateFileSettings const &_FileSettings) -> CEJsonSorted
			{
				CEJsonSorted FileSettings;

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
		auto CaptureScope = co_await g_CaptureExceptions;

		auto pDomains = mp_State.m_StateDatabase.m_Data.f_GetMember("Domains");
		if (!pDomains)
			co_return {};

		for (auto &DomainObject : pDomains->f_Object())
		{
			auto &Name = DomainObject.f_Name();
			if (!fg_IsValidHostname(Name))
				DMibError("'{}' is not a valid Domain name"_f << Name);

			auto &DomainJson = DomainObject.f_Value();

			CDomainSettings Settings;
			fp_ParseSettings(DomainJson["Settings"], Settings);

			auto &Domain = mp_Domains[Name];
			Domain.m_Settings = fg_Move(Settings);
		}

		for (auto &Domain : mp_Domains)
			fp_UpdateDomainSettings(Domain.f_GetName()) > fg_LogError("Mib/WebApp/WebCertificateManager", "Failed to update domain settings");

		co_return {};
	}
}
