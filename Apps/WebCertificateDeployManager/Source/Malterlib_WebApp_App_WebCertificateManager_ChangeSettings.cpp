// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebCertificateManager.h"

#include <Mib/Process/Platform>

#ifndef DPlatformFamily_Windows
#include <Mib/Core/PlatformSpecific/PosixErrNo>
#include <signal.h>
#include <errno.h>
#endif

namespace NMib::NWebApp::NWebCertificateManager
{
	TCFuture<void> CWebCertificateManagerActor::fp_UpdateDomainSettings(CStr const &_DomainName)
	{
		CDomain *pDomain;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					pDomain = mp_Domains.f_FindEqual(_DomainName);
					if (!pDomain)
						return DMibErrorInstance("Domain removed");

					return {};
				}
			)
		;

		if (pDomain->m_CertificateDeploySubscription)
			co_await fg_Exchange(pDomain->m_CertificateDeploySubscription, nullptr)->f_Destroy();

		CWebCertificateDeployActor::CDomainSettings DomainSettings;
		DomainSettings.m_DomainName = _DomainName;

		auto fGetFilesSettings = [&](CCertificateLocation const &_Location)
			{
				CWebCertificateDeployActor::CCertificateFileSettings FileSettingsKey;
				FileSettingsKey.m_Path = _Location.m_Key;
				FileSettingsKey.m_Attributes = pDomain->m_Settings.m_FileSettings_Key.m_Attributes;
				FileSettingsKey.m_User = pDomain->m_Settings.m_FileSettings_Key.m_User;
				FileSettingsKey.m_Group = pDomain->m_Settings.m_FileSettings_Key.m_Group;

				CWebCertificateDeployActor::CCertificateFileSettings FileSettingsFullChain;
				FileSettingsFullChain.m_Path = _Location.m_FullChain;
				FileSettingsFullChain.m_Attributes = pDomain->m_Settings.m_FileSettings_Certificate.m_Attributes;
				FileSettingsFullChain.m_User = pDomain->m_Settings.m_FileSettings_Certificate.m_User;
				FileSettingsFullChain.m_Group = pDomain->m_Settings.m_FileSettings_Certificate.m_Group;

				return CWebCertificateDeployActor::CCertificateFilesSettings{FileSettingsKey, FileSettingsFullChain};
			}
		;

		if (pDomain->m_Settings.m_Location_Ec)
			DomainSettings.m_FileSettings_Ec = fGetFilesSettings(*pDomain->m_Settings.m_Location_Ec);

		if (pDomain->m_Settings.m_Location_Rsa)
			DomainSettings.m_FileSettings_Rsa = fGetFilesSettings(*pDomain->m_Settings.m_Location_Rsa);

		DomainSettings.m_fOnStatusChange = g_ActorFunctor / [this, _DomainName](CHostInfo &&_HostInfo, CWebCertificateDeployActor::CDomainStatus &&_Status) -> TCFuture<void>
			{
				auto pDomain = mp_Domains.f_FindEqual(_DomainName);
				if (!pDomain)
					co_return {};

				pDomain->m_Statuses[_HostInfo] = fg_Move(_Status);

				co_return {};
			}
		;

		DomainSettings.m_fOnCertificateUpdated = g_ActorFunctor / [this](CStr &&_DomainName, CWebCertificateDeployActor::ECertificate _Certificate) -> TCFuture<void>
			{
				auto pDomain = mp_Domains.f_FindEqual(_DomainName);
				if (!pDomain)
					co_return {};

				if (!pDomain->m_Settings.m_Location_NginxPid)
					co_return {};

#ifdef DPlatformFamily_Windows
				co_return DMibErrorInstance("Reloading nginx config is not supported on Windows");
#else
				co_await
					(
						g_Dispatch(mp_FileActor) / [PidLocation = *pDomain->m_Settings.m_Location_NginxPid]
						{
							auto PidText = CFile::fs_ReadStringFromFile(PidLocation);
							auto ProcessId = PidText.f_ToInt(int32(0));
							if (ProcessId <= 0)
								return;

							if (kill(ProcessId, SIGHUP))
								DMibError(NMib::NPlatform::fg_FormatErrno(NMib::NStr::CStr::CFormat("kill({}, SIGHUP) when reloading nginx config") << ProcessId, errno));
						}
					)
				;

				co_return {};
#endif
			}
		;

		pDomain->m_CertificateDeploySubscription = co_await mp_CertificateDeployActor(&CWebCertificateDeployActor::f_AddDomain, fg_Move(DomainSettings));

		co_return {};
	}


	TCFuture<uint32> CWebCertificateManagerActor::fp_CommandLine_DomainChangeSettings(CEJSON const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		if (!fg_IsValidHostname(Name))
			co_return Auditor.f_Exception("'{}' is not a valid domain name"_f << Name);

		if (!mp_Domains.f_FindEqual(Name))
			co_return Auditor.f_Exception("Domain '{}' does not exist"_f << Name);

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					if (!mp_Domains.f_FindEqual(Name))
						return DMibErrorInstance("Domain removed");

					return {};
				}
			)
		;

		auto &Domain = mp_Domains[Name];

		CDomainSettings Settings = Domain.m_Settings;

		{
			auto CaptureScope = co_await (g_CaptureExceptions % Auditor);
			fp_ParseCommandLineSettings(_Params, Settings);
		}

		if (Domain.m_Settings == Settings)
			co_return Auditor.f_Exception("No setting changed");

		Domain.m_Settings = fg_Move(Settings);

		fp_SaveState(Domain);

		co_await (mp_State.m_StateDatabase.f_Save() % "[Change domain settings] Failed to save state" % Auditor);

		Auditor.f_Info("Changed domain settings '{}'"_f << Name);

		co_await self(&CWebCertificateManagerActor::fp_UpdateDomainSettings, Name);

		co_return 0;
	}
}
