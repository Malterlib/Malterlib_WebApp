// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/CommandLine/TableRenderer>

#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	TCFuture<uint32> CWebCertificateManagerActor::fp_CommandLine_DomainList(CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
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
		fAddHeading("EC Location");
		fAddHeading("RSA Location");
		fAddHeading("NGNIX PID location");
		fAddHeading("Certificate File Settings");
		fAddHeading("Key File Settings");
		fAddHeading("Status", false);

		TableRenderer.f_AddHeadingsVector(Headings);

		TableRenderer.f_SetOptions(CTableRenderHelper::EOption_Rounded | CTableRenderHelper::EOption_AvoidRowSeparators);

		for (auto &Domain : mp_Domains)
		{
			auto &Name = Domain.f_GetName();
			if (!DomainName.f_IsEmpty() && DomainName != Name)
				continue;

			CStr Status;

			if (Domain.m_Statuses.f_IsEmpty())
				Status = "{}No secrets manager found, or not yet updated{}"_f << AnsiEncoding.f_StatusWarning() << AnsiEncoding.f_Default();
			else
			{
				for (auto &DomainStatus : Domain.m_Statuses)
				{
					CStr ColorPrefix;

					switch (DomainStatus.m_Severity)
					{
					case CWebCertificateDeployActor::EStatusSeverity_Info: break;
					case CWebCertificateDeployActor::EStatusSeverity_Success: ColorPrefix = AnsiEncoding.f_StatusNormal(); break;
					case CWebCertificateDeployActor::EStatusSeverity_Warning: ColorPrefix = AnsiEncoding.f_StatusWarning(); break;
					case CWebCertificateDeployActor::EStatusSeverity_Error: ColorPrefix = AnsiEncoding.f_StatusError(); break;
					}

					CStr Description;
					if (ColorPrefix)
						Description = "{}{}{}"_f << ColorPrefix<< DomainStatus.m_Description << AnsiEncoding.f_Default();
					else
						Description = DomainStatus.m_Description;

					fg_AddStrSep(Status, "{}: {}"_f << Domain.m_Statuses.fs_GetKey(DomainStatus).f_GetDescColored(_pCommandLine->m_AnsiFlags) << Description, "\n");
				}
			}

			auto fFormatLocation = [&](TCOptional<CCertificateLocation> const &_Location) -> CStr
				{
					if (!_Location)
						return "DISABLED";

					CStr Return;
					Return += "Full chain: {}\n"_f << _Location->m_FullChain;
					Return += "Key: {}\n"_f << _Location->m_Key;

					return Return;
				}
			;

			auto fFormatFileSettings = [&](TCOptional<CCertificateFileSettings> const &_Settings) -> CStr
				{
					if (!_Settings)
						return "DISABLED";

					CStr Return;
					Return += "User: {}\n"_f << _Settings->m_User;
					Return += "Group: {}\n"_f << _Settings->m_Group;
					Return += "Attributes: {vs,vb}\n"_f << fsp_GenerateAttributes(_Settings->m_Attributes).f_Array();

					return Return;
				}
			;

			TableRenderer.f_AddRow
				(
					Name
					, fFormatLocation(Domain.m_Settings.m_Location_Ec)
					, fFormatLocation(Domain.m_Settings.m_Location_Rsa)
					, Domain.m_Settings.m_Location_NginxPid ? *Domain.m_Settings.m_Location_NginxPid : CStr("DISABLED")
					, fFormatFileSettings(Domain.m_Settings.m_FileSettings_Certificate)
					, fFormatFileSettings(Domain.m_Settings.m_FileSettings_Key)
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
