// Copyright © 2020 Nonna Holding AB
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

		auto SettingsOption_GenerateRSA = "GenerateRSA?"_o=
			{
				"Names"_o= {"--generate-rsa"}
				, "Default"_o= false
				, "Type"_o= true
				, "Description"_o= "Generate a RSA based certificate"
			}
		;
		auto SettingsOption_GenerateEC = "GenerateEC?"_o=
			{
				"Names"_o= {"--generate-ec"}
				, "Default"_o= true
				, "Type"_o= true
				, "Description"_o= "Generate a elliptic key based certificate"
			}
		;
		auto SettingsOption_IncludeWildcard = "IncludeWildcard?"_o=
			{
				"Names"_o= {"--include-wildcard"}
				, "Default"_o= true
				, "Type"_o= true
				, "Description"_o= "Generate a certificate with wildcard subdomains"
			}
		;
		auto SettingsOption_EllipticCurveType = "EllipticCurveType?"_o=
			{
				"Names"_o= {"--elliptic-curve-type"}
				, "Default"_o= "secp384r1"
				, "Type"_o= COneOf{"secp256r1", "secp384r1", "secp521r1", "X25519"}
				, "Description"_o= "The type of elliptic curve to use for the EC certificate."
			}
		;
		auto SettingsOption_RSAKeyLength = "RSAKeyLength?"_o=
			{
				"Names"_o= {"--rsa-key-length"}
				, "Default"_o= 4096
				, "Type"_o= COneOf{3072, 4096, 6144, 8192, 12288, 16384}
				, "Description"_o= "The number of bits to use for the RSA key"
			}
		;
		auto SettingsOption_AcmeDirectory = "AcmeDirectory?"_o=
			{
				"Names"_o= {"--acme-directory"}
				, "Type"_o= COneOf{"LetsEncrypt", "LetsEncryptStaging", COneOfType{""}}
				, "Default"_o= "LetsEncryptStaging"
				, "Description"_o= "Generate a elliptic key based certificate"
			}
		;
		auto SettingsOption_AccountKeySettings = "AccountKeySettings?"_o=
			{
				"Names"_o= {"--account-key-type"}
				, "Type"_o= COneOfType{0, COneOf{"default", "secp256r1", "secp384r1", "secp521r1"}}
				, "Default"_o= "default"
				, "Description"_o= "The key type to use for the account. Default will use secp521r1 or secp384r1 if Let's encrypt directory is used."
			}
		;
		auto SettingsOption_ManualDNSChallenge = "ManualDNSChallenge?"_o=
			{
				"Names"_o= {"--manual-dns-challenge-release"}
				, "Type"_o= false
				, "Default"_o= false
				, "Description"_o= "Wait for DNS challenge to be manually released before continuing."
			}
		;
		auto SettingsOption_AlternateChain = "AlternateChain?"_o=
			{
				"Names"_o= {"--alternate-chain"}
				, "Type"_o= ""
				, "Description"_o= "Common name of root for alternate chain to use."
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
						, "CreateAccountKey?"_o=
						{
							"Names"_o= {"--create-account-key"}
							, "Default"_o= true
							, "Description"_o= "Create ACME account private key if not already created"
						}
						, SettingsOption_AcmeDirectory
						, SettingsOption_GenerateRSA
						, SettingsOption_GenerateEC
						, SettingsOption_IncludeWildcard
						, SettingsOption_EllipticCurveType
						, SettingsOption_RSAKeyLength
						, SettingsOption_AccountKeySettings
						, SettingsOption_ManualDNSChallenge
						, SettingsOption_AlternateChain
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainAdd, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--domain-create-account-key"}
					, "Description"_o= "Creates the account key for the account if it hasn't already been created\n"
					, "Options"_o=
					{
						"Domain"_o=
						{
							"Names"_o= {"--domain"}
							, "Type"_o= ""
							, "Description"_o= "The domain name"
						}
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainCreateAccountKey, _Params, _pCommandLine);
				}
			)
		;
		DomainManagement.f_RegisterCommand
			(
				{
					"Names"_o= {"--domain-release-dns-challenge"}
					, "Description"_o= "Releases a domain that has manual DNS challenge release enabled\n"
					, "Options"_o=
					{
						"Domain"_o=
						{
							"Names"_o= {"--domain"}
							, "Type"_o= ""
							, "Description"_o= "The domain name"
						}
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainReleaseDNSChallenge, _Params, _pCommandLine);
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
						, fStripDefault(SettingsOption_AcmeDirectory)
						, fStripDefault(SettingsOption_GenerateRSA)
						, fStripDefault(SettingsOption_GenerateEC)
						, fStripDefault(SettingsOption_IncludeWildcard)
						, fStripDefault(SettingsOption_EllipticCurveType)
						, fStripDefault(SettingsOption_RSAKeyLength)
						, fStripDefault(SettingsOption_AccountKeySettings)
						, fStripDefault(SettingsOption_ManualDNSChallenge)
						, fStripDefault(SettingsOption_AlternateChain)
					}
				}
				, [this](CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
				{
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainChangeSettings, _Params, _pCommandLine);
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
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainList, _Params, _pCommandLine);
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
					return g_Future <<= self(&CAcmeManagerActor::fp_CommandLine_DomainRemove, _Params, _pCommandLine);
				}
			)
		;
	}
}
