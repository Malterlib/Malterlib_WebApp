// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/CommandLine/TableRenderer>

#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	void CWebCertificateManagerActor::fp_ParseCommandLineSettings(CEJSONSorted const &_Params, CDomainSettings &o_Settings)
	{
		CDomainSettings NewSettings = o_Settings;

		auto fParseLocation = [&](TCOptional<CCertificateLocation> &_Previous, CStr const &_Type) -> TCOptional<CCertificateLocation>
			{
				CCertificateLocation Location;
				int32 nLocation = 0;

				auto fSetLocation = [&](CStr CCertificateLocation::*_pMember, CStr const &_MemberName)
					{
						if (auto pValue = _Params.f_GetMember(_MemberName, EJSONType_String))
						{
							if (!CFile::fs_IsPathAbsolute(pValue->f_String()))
								DMibError("Path '{}' should be absolute"_f << pValue->f_String());
							Location.*_pMember = pValue->f_String();
							++nLocation;
						}
						else if (_Previous && !_Params.f_GetMember(_MemberName, EJSONType_Null))
						{
							Location.*_pMember = (*_Previous).*_pMember;
							++nLocation;
						}
					}
				;

				fSetLocation(&CCertificateLocation::m_FullChain, "Location{}Certificate"_f << _Type);
				fSetLocation(&CCertificateLocation::m_Key, "Location{}Key"_f << _Type);

				if (nLocation == 0)
					return {};
				else if (nLocation == 1)
					DMibError("You need to specify both key and certificate locations for {}"_f << _Type);

				return Location;
			}
		;

		NewSettings.m_Location_Rsa = fParseLocation(NewSettings.m_Location_Rsa, "Rsa");
		NewSettings.m_Location_Ec = fParseLocation(NewSettings.m_Location_Ec, "Ec");

		if (_Params.f_GetMember("LocationNginxPid", EJSONType_Null))
			NewSettings.m_Location_NginxPid.f_Clear();
		else if (auto pValue = _Params.f_GetMember("LocationNginxPid", EJSONType_String))
		{
			if (!CFile::fs_IsPathAbsolute(pValue->f_String()))
				DMibError("Path '{}' should be absolute"_f << pValue->f_String());

			NewSettings.m_Location_NginxPid = pValue->f_String();
		}

		auto fParseFileSettings = [&](CCertificateFileSettings &o_FileSettings, CStr const &_Type)
			{
				if (auto pValue = _Params.f_GetMember("{}FileUser"_f << _Type, EJSONType_String))
					o_FileSettings.m_User = pValue->f_String();

				if (auto pValue = _Params.f_GetMember("{}FileGroup"_f << _Type, EJSONType_String))
					o_FileSettings.m_Group = pValue->f_String();

				if (auto pValue = _Params.f_GetMember("{}FileAttributes"_f << _Type, EJSONType_Array))
					o_FileSettings.m_Attributes = fsp_ParseAttributes(*pValue, o_FileSettings.m_Attributes);
			}
		;

		fParseFileSettings(NewSettings.m_FileSettings_Certificate, "Certificate");
		fParseFileSettings(NewSettings.m_FileSettings_Key, "Key");

		o_Settings = NewSettings;
	}

	void CWebCertificateManagerActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				"Malterlib Web Certificate Manager"
				, "Deploys web certificates to file system."
			)
		;

		auto DomainManagement = o_CommandLine.f_AddSection("Domain Management", "Commands to manage WebCertificateManager domains");

		auto SettingsOption_LocationRsaCertificate = "LocationRsaCertificate?"_o=
			{
				"Names"_o= {"--location-rsa-certificate"}
				, "Default"_o= nullptr
				, "Type"_o= COneOfType{COneOf(nullptr), ""}
				, "Description"_o= "File location to deploy RSA certificate to"
			}
		;
		auto SettingsOption_LocationRsaKey = "LocationRsaKey?"_o=
			{
				"Names"_o= {"--location-rsa-key"}
				, "Default"_o= nullptr
				, "Type"_o= COneOfType{COneOf(nullptr), ""}
				, "Description"_o= "File location to deploy RSA key to"
			}
		;
		auto SettingsOption_LocationEcCertificate = "LocationEcCertificate?"_o=
			{
				"Names"_o= {"--location-ec-certificate"}
				, "Default"_o= nullptr
				, "Type"_o= COneOfType{COneOf(nullptr), ""}
				, "Description"_o= "File location to deploy EC certificate to"
			}
		;
		auto SettingsOption_LocationEcKey = "LocationEcKey?"_o=
			{
				"Names"_o= {"--location-ec-key"}
				, "Default"_o= nullptr
				, "Type"_o= COneOfType{COneOf(nullptr), ""}
				, "Description"_o= "File location to deploy EC key to"
			}
		;
		auto SettingsOption_LocationNginxPid = "LocationNginxPid?"_o=
			{
				"Names"_o= {"--location-nginx-pid"}
				, "Default"_o= nullptr
				, "Type"_o= COneOfType{COneOf(nullptr), ""}
				, "Description"_o= "File location where nginx pid file lives.\n"
				"After deploying new certificates this location will de used to read the PID to send a HUP signal to make nginx reload the configuration."
			}
		;
		auto SettingsOption_CertificateFileUser = "CertificateFileUser?"_o=
			{
				"Names"_o= {"--certificate-file-user"}
				, "Default"_o= ""
				, "Type"_o= ""
				, "Description"_o= "The user that should own the certificate files.\n"
			}
		;
		auto SettingsOption_CertificateFileGroup = "CertificateFileGroup?"_o=
			{
				"Names"_o= {"--certificate-file-group"}
				, "Default"_o= ""
				, "Type"_o= ""
				, "Description"_o= "The group that should own the certificate files.\n"
			}
		;

		COneOf AllAttributes;
		AllAttributes.m_Config = CEJSONOrdered::fs_FromCompatible(fsp_GenerateAttributes(EFileAttrib_AllUnixPermissions));

		auto SettingsOption_CertificateFileAttributes = "CertificateFileAttributes?"_o=
			{
				"Names"_o= {"--certificate-file-attributes"}
				, "Default"_o= CEJSONOrdered{"UserRead", "UserWrite", "GroupRead", "EveryoneRead"}
				, "Type"_o= CEJSONOrdered{AllAttributes}
				, "Description"_o= "The file attributes that should be used for certificate files.\n"
			}
		;
		auto SettingsOption_KeyFileUser = "KeyFileUser?"_o=
			{
				"Names"_o= {"--key-file-user"}
				, "Default"_o= ""
				, "Type"_o= ""
				, "Description"_o= "The user that should own the key files.\n"
			}
		;
		auto SettingsOption_KeyFileGroup = "KeyFileGroup?"_o=
			{
				"Names"_o= {"--key-file-group"}
				, "Default"_o= ""
				, "Type"_o= ""
				, "Description"_o= "The group that should own the key files.\n"
			}
		;
		auto SettingsOption_KeyFileAttributes = "KeyFileAttributes?"_o=
			{
				"Names"_o= {"--key-file-attributes"}
				, "Default"_o= CEJSONOrdered{"UserRead", "UserWrite"}
				, "Type"_o= CEJSONOrdered{AllAttributes}
				, "Description"_o= "The file attributes that should be used for key files.\n"
			}
		;


		auto fStripDefault = [](auto &&_Template)
			{
				auto Return = _Template;
				Return.m_Value.f_RemoveMember("Default");
				return Return;
			}
		;

		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--domain-add"}
					, "Description"_o= "Adds an application domain\n"
					, "Options"_o=
					{
						"Domain"_o=
						{
							"Names"_o= {"--domain"}
							, "Type"_o= ""
							, "Description"_o= "The domain name"
						}
						, SettingsOption_LocationRsaCertificate
						, SettingsOption_LocationRsaKey
						, SettingsOption_LocationEcCertificate
						, SettingsOption_LocationEcKey
#ifndef DPlatformFamily_Windows
						, SettingsOption_LocationNginxPid
#endif
						, SettingsOption_CertificateFileUser
						, SettingsOption_CertificateFileGroup
						, SettingsOption_CertificateFileAttributes
						, SettingsOption_KeyFileUser
						, SettingsOption_KeyFileGroup
						, SettingsOption_KeyFileAttributes
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CWebCertificateManagerActor::fp_CommandLine_DomainAdd, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--domain-change-settings"}
					, "Description"_o= "Change settings for domain.\n"
					, "Options"_o=
					{
						"Domain"_o=
						{
							"Names"_o= {"--domain"}
							, "Type"_o= ""
							, "Description"_o= "Unique name of the domain to change settings for."
						}
						, fStripDefault(SettingsOption_LocationRsaCertificate)
						, fStripDefault(SettingsOption_LocationRsaKey)
						, fStripDefault(SettingsOption_LocationEcCertificate)
						, fStripDefault(SettingsOption_LocationEcKey)
#ifndef DPlatformFamily_Windows
						, fStripDefault(SettingsOption_LocationNginxPid)
#endif
						, fStripDefault(SettingsOption_CertificateFileUser)
						, fStripDefault(SettingsOption_CertificateFileGroup)
						, fStripDefault(SettingsOption_CertificateFileAttributes)
						, fStripDefault(SettingsOption_KeyFileUser)
						, fStripDefault(SettingsOption_KeyFileGroup)
						, fStripDefault(SettingsOption_KeyFileAttributes)
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CWebCertificateManagerActor::fp_CommandLine_DomainChangeSettings, _Params, _pCommandLine);
				}
			)
		;

		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--domain-list"}
					, "Description"_o= "List domains."
					, "Options"_o=
					{
						"Verbose?"_o=
						{
							"Names"_o= {"--verbose", "-v"}
							, "Default"_o= false
							, "Description"_o= "Display more extensive information about the domain."
						}
						, "Domain?"_o=
						{
							"Names"_o= {"--domain"}
							, "Default"_o= ""
							, "Description"_o= "Unique name of the domain to list. Leave empty to list all domains."
						}
						, CTableRenderHelper::fs_OutputTypeOption()
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CWebCertificateManagerActor::fp_CommandLine_DomainList, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--domain-remove"}
					, "Description"_o= "Remove the domain."
					, "Parameters"_o=
					{
						"Domain"_o=
						{
							"Type"_o= ""
							, "Description"_o= "The name of the domain to remove."
						}
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CWebCertificateManagerActor::fp_CommandLine_DomainRemove, _Params, _pCommandLine);
				}
			)
		;
	}
}
