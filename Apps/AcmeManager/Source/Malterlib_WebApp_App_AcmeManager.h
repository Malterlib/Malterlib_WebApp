// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Concurrency/DistributedDaemon>
#include <Mib/Concurrency/ActorSequencerActor>
#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cloud/SecretsManager>
#include <Mib/Cryptography/Certificate>
#include <Mib/Web/AWS/Route53>
#include <Mib/Web/ACME>
#include <Mib/Web/Curl>

namespace NMib::NWebApp::NAcmeManager
{
	struct CAcmeManagerActor : public CDistributedAppActor
	{
		CAcmeManagerActor();
		~CAcmeManagerActor();

	private:
		struct CDomainSettings
		{
			bool operator == (CDomainSettings const &_Right) const;
			auto f_Tuple() const;
			CPublicKeySetting f_PublicKeySettings() const;

			EPublicKeyType m_EllipticCurveType;
			CPublicKeySettings_RSA m_RSASettings;
			bool m_bGenerateRSA = true;
			bool m_bGenerateEC = true;
			bool m_bIncludeWildcard = true;
			bool m_bManualDNSChallenge = false;
			CStr m_AlternateChain;
			CAcmeClientActor::EDefaultDirectory m_AcmeDirectory = CAcmeClientActor::EDefaultDirectory_LetsEncryptStaging;
			CStr m_AcmeCustomDirectory;
			CPublicKeySetting m_AccountKeySettings = CPublicKeySettings_EC_secp521r1{};
		};

		struct CDomainState
		{
			CSecureByteVector m_ACMEAccountPrivateKey;
			TCDistributedActor<CSecretsManager> m_SecretsManager;
			CHostInfo m_SecretsManagerHostInfo;
			TCActor<CAcmeClientActor> m_AcmeClientRSA;
			TCActor<CAcmeClientActor> m_AcmeClientEC;
			TCVector<TCPromise<void>> m_OnReleaseDNSChallenge;
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
			CStr m_Description;
			EStatusSeverity m_Severity = EStatusSeverity_Info;
		};

		struct CDomain
		{
			CStr const &f_GetName() const
			{
				return TCMap<CStr, CDomain>::fs_GetKey(*this);
			}
			CStr f_GetSecretFolder() const;
			CDomainStatus const *f_GetCurrentStatus() const;

			TCMap<CHostInfo, CDomainStatus> m_Statuses;
			TCOptional<CDomainState> m_DomainState;
			CDomainSettings m_Settings;
			CSequencer m_UpdateDomainSequencer{"AcmeManagerActor Domain {}"_f << f_GetName()};
		};

		void fp_BuildCommandLine(CDistributedAppCommandLineSpecification &o_CommandLine) override;

		TCFuture<void> fp_StartApp(NEncoding::CEJsonSorted const _Params) override;
		TCFuture<void> fp_StopApp() override;

		TCFuture<uint32> fp_CommandLine_DomainList(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainAdd(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainCreateAccountKey(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainReleaseDNSChallenge(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainChangeSettings(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);
		TCFuture<uint32> fp_CommandLine_DomainRemove(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine);

		TCFuture<void> fp_ReadState();
		void fp_ParseSettings(CEJsonSorted const &_Params, CDomainSettings &o_Settings);
		CEJsonSorted fp_SaveSettings(CDomainSettings const &_Settings);
		void fp_SaveState(CDomain const &_Domain);

		static EPublicKeyType fsp_EllipticCurveTypeFromStr(CStr const &_String);
		static CStr fsp_EllipticCurveTypeToStr(EPublicKeyType _Type);

		TCFuture<void> fp_UpdateAllDomains(CStr _CreateAccountForDomainName);

		TCFuture<void> fp_HandleSecretsManagerAdded(TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _Info);

		TCFuture<void> fp_SecretsManagerAdded(TCDistributedActor<CSecretsManager> _SecretsManager, CTrustedActorInfo _Info, NStr::CStr _CreatePrivateKeyForDomain);
		TCFuture<void> fp_SecretsManagerRemoved(TCWeakDistributedActor<CActor> _SecretsManager, CTrustedActorInfo _ActorInfo);

		void fp_UpdateDomainStatus(CDomain &o_Domain, CHostInfo const &_HostInfo, EStatusSeverity _Severity, CStr const &_Status);

		CEJsonSorted fp_GetConfigValue(CStr const &_Name, CEJsonSorted const &_Default) const;

		[[nodiscard]] NException::CExceptionPointer fp_UpdateDomain_CheckPreconditions(CStr const &_DomainName, CDomain *&o_pDomain, CDomainState *&o_pDomainState);
		TCFuture<bool> fp_UpdateDomain_HandleChallenge
			(
				CStr _DomainName
				, CStr _CertificateType
				, CAcmeClientActor::CChallenge _Challenge
			)
		;
		TCFuture<void> fp_UpdateDomain_RunAcmeProtocol
			(
				CStr _DomainName
				, CStr _CertificateType
				, CPublicKeySetting _PublicKeySettings
				, TCActor<CAcmeClientActor> _AcmeClient
				, CDomainSettings _DomainSettings
				, CEJsonSorted _DomainSettingsJson
			)
		;
		TCFuture<bool> fp_UpdateDomain_IsAlreadyUpToDate(CStr _DomainName, CEJsonSorted _DomainSettingsJson, CStr _CertificateType);
		TCFuture<void> fp_UpdateDomain(CStr _DomainName, bool _bCreateAccountKey);

		TCMap<CStr, CDomain> mp_Domains;

		TCTrustedActorSubscription<CSecretsManager> mp_SecretsManagerSubscription;
		TCMap<TCWeakDistributedActor<CActor>, CStr> mp_LastSecretsManagerError;
		TCSet<TCWeakDistributedActor<CActor>> mp_RetryingSecretsManagers;

		TCRoundRobinActors<CCurlActor> mp_CurlActors{2};
		TCActor<CAwsRoute53Actor> mp_Route53Actor;

		TCVector<CStr> mp_AcmeAccountEmails;

		CActorSubscription mp_DomainUpdateTimerSubscription;
	};
}
