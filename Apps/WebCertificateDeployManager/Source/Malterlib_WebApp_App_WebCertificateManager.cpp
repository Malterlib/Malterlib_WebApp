// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Concurrency/LogError>
#include "Malterlib_WebApp_App_WebCertificateManager.h"

namespace NMib::NWebApp::NWebCertificateManager
{
	CWebCertificateManagerActor::CWebCertificateManagerActor()
		: CDistributedAppActor(CDistributedAppActor_Settings("WebCertificateManager").f_AuditCategory("Malterlib/WebApp/WebCertificateManager"))
	{
	}

	CWebCertificateManagerActor::~CWebCertificateManagerActor() = default;

	CEJSONSorted CWebCertificateManagerActor::fp_GetConfigValue(CStr const &_Name, CEJSONSorted const &_Default) const
	{
		return mp_State.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, _Default);
	}

	TCFuture<void> CWebCertificateManagerActor::fp_StartApp(NEncoding::CEJSONSorted const &_Params)
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

		mp_FileActor = TCActor<CSeparateThreadActor>{fg_Construct(), "Web certificate file access"};
		mp_CertificateDeployActor = fg_Construct(mp_State.m_DistributionManager, mp_State.m_TrustManager, mp_FileActor);

		co_await mp_CertificateDeployActor(&CWebCertificateDeployActor::f_Start);

		co_await fp_ReadState();

		co_return {};
	}

	TCFuture<void> CWebCertificateManagerActor::fp_StopApp()
	{
		TCActorResultVector<void> Destroys;

		for (auto &Domain : mp_Domains)
		{
			if (Domain.m_CertificateDeploySubscription)
				Domain.m_CertificateDeploySubscription->f_Destroy() > Destroys.f_AddResult();
		}

		if (mp_CertificateDeployActor)
			fg_Move(mp_CertificateDeployActor).f_Destroy() > Destroys.f_AddResult();

		co_await Destroys.f_GetResults();

		co_return {};
	}

	auto CWebCertificateManagerActor::CDomainSettings::f_Tuple() const
	{
		return fg_TupleReferences(m_Location_Ec, m_Location_Rsa, m_Location_NginxPid, m_FileSettings_Certificate, m_FileSettings_Key);
	}

	bool CWebCertificateManagerActor::CDomainSettings::operator == (CWebCertificateManagerActor::CDomainSettings const &_Right) const
	{
		return f_Tuple() == _Right.f_Tuple();
	}

	auto CWebCertificateManagerActor::CCertificateLocation::f_Tuple() const
	{
		return fg_TupleReferences(m_Key, m_FullChain);
	}

	bool CWebCertificateManagerActor::CCertificateLocation::operator == (CCertificateLocation const &_Right) const
	{
		return f_Tuple() == _Right.f_Tuple();
	}

	auto CWebCertificateManagerActor::CCertificateFileSettings::f_Tuple() const
	{
		return fg_TupleReferences(m_User, m_Group, m_Attributes);
	}

	bool CWebCertificateManagerActor::CCertificateFileSettings::operator == (CCertificateFileSettings const &_Right) const
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
