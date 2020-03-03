// Copyright © 2018 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/CommandLine/TableRenderer>

#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<uint32> CAcmeManagerActor::fp_CommandLine_DomainList(CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		bool bVerbose = _Params["Verbose"].f_Boolean();
		CStr DomainName = _Params["Domain"].f_String();

		auto AnsiEncoding = _pCommandLine->f_AnsiEncoding();
		CTableRenderHelper TableRenderer = _pCommandLine->f_TableRenderer();

		TCVector<CStr> Headings;
		TCSet<mint> VerboseHeadings;

		auto fAddHeading = [&](CStr const &_Name, bool _bVerbose = true)
			{
				if (_bVerbose)
					VerboseHeadings[Headings.f_GetLen()];

				Headings.f_Insert(_Name);
			}
		;

		fAddHeading("Domain", false);
		fAddHeading("Generate EC");
		fAddHeading("Generate RSA");
		fAddHeading("EC Type");
		fAddHeading("RSA Key Length");
		fAddHeading("Include Wildcard");
		fAddHeading("ACME Directory");
		fAddHeading("Account Key Type");
		fAddHeading("Status", false);

		TableRenderer.f_AddHeadingsVector(Headings);
		TableRenderer.f_SetOptions(CTableRenderHelper::EOption_Rounded | CTableRenderHelper::EOption_AvoidRowSeparators);

		for (auto &Domain : mp_Domains)
		{
			auto &Name = Domain.f_GetName();
			if (!DomainName.f_IsEmpty() && DomainName != Name)
				continue;

			CStr AcmeDirectory;
			switch (Domain.m_Settings.m_AcmeDirectory)
			{
			case CAcmeClientActor::EDefaultDirectory_LetsEncrypt:
				AcmeDirectory = "Let's Encrypt";
				break;
			case CAcmeClientActor::EDefaultDirectory_LetsEncryptStaging:
				AcmeDirectory = "Let's Encrypt Staging";
				break;
			case CAcmeClientActor::EDefaultDirectory_Custom:
				AcmeDirectory = Domain.m_Settings.m_AcmeCustomDirectory;
				break;
			}

			CStr AccountKeyType = "EC secp521r1";
			switch (Domain.m_Settings.m_AccountKeySettings.f_GetTypeID())
			{
			case EPublicKeyType_EC_secp256r1: AccountKeyType = "EC secp256r1"; break;
			case EPublicKeyType_EC_secp384r1: AccountKeyType = "EC secp384r1"; break;
			case EPublicKeyType_EC_secp521r1: AccountKeyType = "EC secp521r1"; break;
			case EPublicKeyType_RSA: AccountKeyType = "RSA {}"_f << Domain.m_Settings.m_AccountKeySettings.f_Get<EPublicKeyType_RSA>().m_KeyLength; break;
			default: break;
			}

			CStr Status;

			if (Domain.m_Statuses.f_IsEmpty())
				Status = "{}No secrets manager found, or not yet updated{}"_f << AnsiEncoding.f_StatusWarning() << AnsiEncoding.f_Default();
			else
			{
				for (auto &DomainStatus : Domain.m_Statuses)
				{
					CStr Description;
					switch (DomainStatus.m_Severity)
					{
					case EStatusSeverity_Info: Description = DomainStatus.m_Description; break;
					case EStatusSeverity_Success: Description = "{}{}{}"_f << AnsiEncoding.f_StatusNormal() << DomainStatus.m_Description << AnsiEncoding.f_Default(); break;
					case EStatusSeverity_Warning: Description = "{}{}{}"_f << AnsiEncoding.f_StatusWarning() << DomainStatus.m_Description << AnsiEncoding.f_Default(); break;
					case EStatusSeverity_Error: Description = "{}{}{}"_f << AnsiEncoding.f_StatusError() << DomainStatus.m_Description << AnsiEncoding.f_Default(); break;
					}
					fg_AddStrSep(Status, "{}: {}"_f << Domain.m_Statuses.fs_GetKey(DomainStatus).f_GetDescColored(_pCommandLine->m_AnsiFlags) << Description, "\n");
				}
			}

			TableRenderer.f_AddRow
				(
				 	Name
				 	, Domain.m_Settings.m_bGenerateEC ? "true" : "false"
				 	, Domain.m_Settings.m_bGenerateRSA ? "true" : "false"
				 	, fsp_EllipticCurveTypeToStr(Domain.m_Settings.m_EllipticCurveType)
				 	, "{}"_f << Domain.m_Settings.m_RSASettings.m_KeyLength
				 	, Domain.m_Settings.m_bIncludeWildcard ? "true" : "false"
					, AcmeDirectory
					, AccountKeyType
				 	, Status
				)
			;
		}

 		if (!bVerbose)
		{
			while (auto pLargest = VerboseHeadings.f_FindLargest())
			{
				TableRenderer.f_RemoveColumn(*pLargest);
				VerboseHeadings.f_Remove(pLargest);
			}
		}

		TableRenderer.f_Output(_Params);

		co_return 0;
	}
}
