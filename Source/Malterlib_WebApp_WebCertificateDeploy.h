// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedActor>
#include <Mib/Concurrency/DistributedActorTrustManager>
#include <Mib/File/File>

namespace NMib::NWebApp
{
	struct CWebCertificateDeployActor : public NConcurrency::CActor
	{
		CWebCertificateDeployActor
			(
				NConcurrency::TCActor<NConcurrency::CActorDistributionManager> const &_DistributionManager
				, NConcurrency::TCActor<NConcurrency::CDistributedActorTrustManager> const &_TrustManager
			)
		;
		~CWebCertificateDeployActor();

		NConcurrency::TCFuture<void> f_Start();

		struct CCertificateFileSettings
		{
			NStr::CStr m_Path;
			NStr::CStr m_User;
			NStr::CStr m_Group;
			NFile::EFileAttrib m_Attributes = NFile::EFileAttrib_UnixAttributesValid | NFile::EFileAttrib_UserRead | NFile::EFileAttrib_UserWrite;
		};

		struct CCertificateFilesSettings
		{
			CCertificateFileSettings m_Key;
			CCertificateFileSettings m_FullChain;
		};

		enum EStatusSeverity
		{
			EStatusSeverity_Info
			, EStatusSeverity_Success
			, EStatusSeverity_Warning
			, EStatusSeverity_Error
		};

		struct CDomainStatus
		{
			auto operator <=> (CDomainStatus const &_Right) const noexcept = default;

			NStr::CStr m_Description;
			EStatusSeverity m_Severity = EStatusSeverity_Info;
		};

		enum ECertificate
		{
			ECertificate_Ec
			, ECertificate_Rsa
		};

		struct CDomainSettings
		{
			NStr::CStr m_DomainName;

			NStorage::TCOptional<CCertificateFilesSettings> m_FileSettings_Ec;
			NStorage::TCOptional<CCertificateFilesSettings> m_FileSettings_Rsa;

			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NConcurrency::CHostInfo _HostInfo, CDomainStatus _Status)> m_fOnStatusChange;
			NConcurrency::TCActorFunctor<NConcurrency::TCFuture<void> (NStr::CStr _DomainName, ECertificate _Certificate)> m_fOnCertificateUpdated;
		};

		NConcurrency::TCFuture<NConcurrency::CActorSubscription> f_AddDomain(CDomainSettings _DomainSettings);

	private:
		NConcurrency::TCFuture<void> fp_Destroy() override;

		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NWebApp;
#endif
