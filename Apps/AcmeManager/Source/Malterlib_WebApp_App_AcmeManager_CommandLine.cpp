// Copyright © 2018 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/CommandLine/TableRenderer>

#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	void CAcmeManagerActor::fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine)
	{
		CDistributedAppActor::fp_BuildCommandLine(o_CommandLine);

		o_CommandLine.f_SetProgramDescription
			(
				"Malterlib ACME Manager"
				, "Manages domain certificates through ACME service."
			)
		;

		auto DomainManagement = o_CommandLine.f_AddSection("Domain Management", "Commands to manage AcmeManager domains");

		auto SettingsOption_GenerateRSA = "GenerateRSA?"_=
			{
				"Names"_= {"--generate-rsa"}
				, "Default"_= true
				, "Type"_= true
				, "Description"_= "Generate a RSA based certificate"
			}
		;
		auto SettingsOption_GenerateEC = "GenerateEC?"_=
			{
				"Names"_= {"--generate-ec"}
				, "Default"_= true
				, "Type"_= true
				, "Description"_= "Generate a elliptic key based certificate"
			}
		;
		auto SettingsOption_IncludeWildcard = "IncludeWildcard?"_=
			{
				"Names"_= {"--include-wildcard"}
				, "Default"_= true
				, "Type"_= true
				, "Description"_= "Generate a certificate with wildcard subdomains"
			}
		;
		auto SettingsOption_EllipticCurveType = "EllipticCurveType?"_=
			{
				"Names"_= {"--elliptic-curve-type"}
				, "Default"_= "secp384r1"
				, "Type"_= COneOf{"secp256r1", "secp384r1", "secp521r1", "X25519"}
				, "Description"_= "The type of elliptic curve to use for the EC certificate."
			}
		;
		auto SettingsOption_RSAKeyLength = "RSAKeyLength?"_=
			{
				"Names"_= {"--rsa-key-length"}
				, "Default"_= 4096
				, "Type"_= COneOf{3072, 4096, 6144, 8192, 12288, 16384}
				, "Description"_= "The number of bits to use for the RSA key"
			}
		;
		auto SettingsOption_AcmeDirectory = "AcmeDirectory?"_=
			{
				"Names"_= {"--acme-directory"}
				, "Type"_= COneOf{"LetsEncrypt", "LetsEncryptStaging", COneOfType{""}}
				, "Default"_= "LetsEncryptStaging"
				, "Description"_= "Generate a elliptic key based certificate"
			}
		;
		auto SettingsOption_AccountKeySettings = "AccountKeySettings?"_=
			{
				"Names"_= {"--account-key-type"}
				, "Type"_= COneOfType{0, COneOf{"default", "secp256r1", "secp384r1", "secp521r1"}}
				, "Default"_= "default"
				, "Description"_= "The key type to use for the account. Default will use secp521r1 or secp384r1 if Let's encrypt directory is used."
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
					"Names"_= {"--domain-add"}
					, "Description"_= "Adds an application domain\n"
					, "Options"_=
					{
						"Domain"_=
						{
							"Names"_= {"--domain"}
							, "Type"_= ""
							, "Description"_= "The domain name"
						}
						, "CreateAccountKey?"_=
						{
							"Names"_= {"--create-account-key"}
							, "Default"_= true
							, "Description"_= "Create ACME account private key if not already created"
						}
						, SettingsOption_GenerateRSA
						, SettingsOption_GenerateEC
						, SettingsOption_IncludeWildcard
						, SettingsOption_EllipticCurveType
						, SettingsOption_RSAKeyLength
						, SettingsOption_AccountKeySettings
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainAdd, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--domain-create-account-key"}
					, "Description"_= "Creates the account key for the account if it hasn't already been created\n"
					, "Options"_=
					{
						"Domain"_=
						{
							"Names"_= {"--domain"}
							, "Type"_= ""
							, "Description"_= "The domain name"
						}
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainCreateAccountKey, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--domain-change-settings"}
					, "Description"_= "Change settings for domain.\n"
					, "Options"_=
					{
						"Domain"_=
						{
							"Names"_= {"--domain"}
							, "Type"_= ""
							, "Description"_= "Unique name of the domain to change settings for."
						}
						, fStripDefault(SettingsOption_GenerateRSA)
						, fStripDefault(SettingsOption_GenerateEC)
						, fStripDefault(SettingsOption_IncludeWildcard)
						, fStripDefault(SettingsOption_EllipticCurveType)
						, fStripDefault(SettingsOption_RSAKeyLength)
						, fStripDefault(SettingsOption_AccountKeySettings)
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainChangeSettings, _Params, _pCommandLine);
				}
			)
		;

		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--domain-list"}
					, "Description"_= "List domains."
					, "Options"_=
					{
						"Verbose?"_=
						{
							"Names"_= {"--verbose", "-v"}
							, "Default"_= false
							, "Description"_= "Display more extensive information about the domain."
						}
						, "Domain?"_=
						{
							"Names"_= {"--domain"}
							, "Default"_= ""
							, "Description"_= "Unique name of the domain to list. Leave empty to list all domains."
						}
						, CTableRenderHelper::fs_OutputTypeOption()
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainList, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_= {"--domain-remove"}
					, "Description"_= "Remove the domain."
					, "Parameters"_=
					{
						"Domain"_=
						{
							"Type"_= ""
							, "Description"_= "The name of the domain to remove."
						}
					}
				}
				, [this](CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainRemove, _Params, _pCommandLine);
				}
			)
		;
	}
}
