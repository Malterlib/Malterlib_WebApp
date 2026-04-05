// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Concurrency/LogError>
#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	CWebCertificateManagerActor::CWebCertificateManagerActor()
		: CDistributedAppActor(CDistributedAppActor_Settings("WebCertificateManager").f_AuditCategory("Malterlib/WebApp/WebCertificateManager"))
	{
	}

	CWebCertificateManagerActor::~CWebCertificateManagerActor() = default;

	CEJsonSorted CWebCertificateManagerActor::fp_GetConfigValue(CStr const &_Name, CEJsonSorted const &_Default) const
	{
		return mp_State.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, _Default);
	}

	TCFuture<void> CWebCertificateManagerActor::fp_StartApp(NEncoding::CEJsonSorted const _Params)
	{
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> NException::CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return DMibErrorInstance("Startup aborted");
					return {};
				}
			)
		;

		mp_CertificateDeployActor = fg_Construct(mp_State.m_DistributionManager, mp_State.m_TrustManager);

		co_await mp_CertificateDeployActor(&CWebCertificateDeployActor::f_Start);

		co_await fp_ReadState();

		co_return {};
	}

	TCFuture<void> CWebCertificateManagerActor::fp_StopApp()
	{
		TCFutureVector<void> Destroys;

		for (auto &Domain : mp_Domains)
		{
			if (Domain.m_CertificateDeploySubscription)
				Domain.m_CertificateDeploySubscription->f_Destroy() > Destroys;
		}

		if (mp_CertificateDeployActor)
			fg_Move(mp_CertificateDeployActor).f_Destroy() > Destroys;

		co_await fg_AllDoneWrapped(Destroys);

		co_return {};
	}

	bool CWebCertificateManagerActor::CDomainSettings::operator == (CWebCertificateManagerActor::CDomainSettings const &_Right) const noexcept
	{
		return f_Tuple() == _Right.f_Tuple();
	}

	bool CWebCertificateManagerActor::CCertificateLocation::operator == (CCertificateLocation const &_Right) const noexcept
	{
		return f_Tuple() == _Right.f_Tuple();
	}

	bool CWebCertificateManagerActor::CCertificateFileSettings::operator == (CCertificateFileSettings const &_Right) const noexcept
	{
		return f_Tuple() == _Right.f_Tuple();
	}
}

namespace NMib::NWebApp
{
	TCActor<CDistributedAppActor> fg_ConstructApp_WebCertificateManager()
	{
		return fg_Construct<NWebCertificateManager::CWebCertificateManagerActor>();
	}
}
