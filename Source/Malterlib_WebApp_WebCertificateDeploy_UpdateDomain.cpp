// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_WebCertificateDeploy_Internal.h"

#include <Mib/Cryptography/Certificate>

namespace NMib::NWebApp
{
	using namespace NCryptography;
	using namespace NTime;
	using namespace NFile;

	TCFuture<void> CWebCertificateDeployActor::CInternal::f_UpdateAllDomainsForAllSecretsManagers()
	{
		TCVector<CStr> DomainNames;
		for (auto &Domain : m_Domains)
		{
			if (Domain.f_GetCurrentStatus() && Domain.f_GetCurrentStatus()->m_Severity == EStatusSeverity_Success)
				continue;
			DomainNames.f_Insert(Domain.m_Settings.m_DomainName);
		}

		if (DomainNames.f_IsEmpty())
			co_return {};

		if (m_SecretsManagerSubscription.m_Actors.f_IsEmpty())
			co_return {};

		DMibLogWithCategory(Mib/WebApp/WebCertificateDeploy, Info, "Updating out of date domains: {vs}", DomainNames);

		TCFutureVector<void> Results;

		for (auto &DomainName : DomainNames)
			f_UpdateDomainForAllSecretsManagers(DomainName) > Results;

		co_await fg_AllDone(Results);

		co_return {};
	}

	TCFuture<void> CWebCertificateDeployActor::CInternal::f_UpdateDomainForSecretsManager
		(
			CStr _DomainName
			, TCDistributedActor<CSecretsManager> _SecretsManager
			, CHostInfo _SecretsManagerHostInfo
		)
	{
		CDomain *pDomain = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (m_pThis->f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					pDomain = m_Domains.f_FindEqual(_DomainName);
					if (!pDomain)
						return DMibErrorInstance("Domain no longer exists");

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(_SecretsManager))
						return DMibErrorInstance("Secret manager no longer exists");

					return {};
				}
			)
		;

		CDomainState DomainState;
		DomainState.m_SecretsManager = _SecretsManager;
		DomainState.m_SecretsManagerHostInfo = _SecretsManagerHostInfo;

		auto UpdateResult = co_await pDomain->m_UpdateDomainSequencer.f_RunSequenced
			(
				g_ActorFunctorWeak /
				[
					this
					, _DomainName
					, DomainState = fg_Move(DomainState)
				]
				(CActorSubscription _Subscription) mutable -> TCFuture<void>
				{
					auto *pDomain = m_Domains.f_FindEqual(_DomainName);

					if (!pDomain)
						co_return {};

					if (!m_SecretsManagerSubscription.m_Actors.f_FindEqual(DomainState.m_SecretsManager))
						co_return {};

					auto &Domain = *pDomain;

					if (auto *pCurrentStatus = Domain.f_GetCurrentStatus())
					{
						if (pCurrentStatus->m_Severity == EStatusSeverity_Success && DomainState.m_SecretsManager != Domain.m_DomainState->m_SecretsManager)
						{
							f_UpdateDomainStatus(Domain, DomainState.m_SecretsManagerHostInfo, EStatusSeverity_Info, "Aborted, another secrets manager already succeeded");
							co_return {};
						}
					}

					Domain.m_DomainState = fg_Move(DomainState);

					co_await
						(
							f_UpdateDomain(Domain.f_GetName())
							% ("Failed to update domain '{}'"_f << Domain.f_GetName())
						)
					;

					(void)_Subscription;

					co_return {};
				}
			)
			.f_Wrap()
		;

		if (!UpdateResult)
			f_UpdateDomainStatus(*pDomain, _SecretsManagerHostInfo, EStatusSeverity_Error, "Error updating domain: {}"_f << UpdateResult.f_GetExceptionStr());

		co_return {};
	}

	TCFuture<void> CWebCertificateDeployActor::CInternal::f_UpdateDomainForAllSecretsManagers(CStr _DomainName)
	{
		CDomain *pDomain = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (m_pThis->f_IsDestroyed())
						return DMibErrorInstance("Shutting down");

					pDomain = m_Domains.f_FindEqual(_DomainName);
					if (!pDomain)
						return DMibErrorInstance("Domain no longer exists");

					return {};
				}
			)
		;

		TCFutureVector<void> UpdateResults;
		for (auto &SecretsManager : m_SecretsManagerSubscription.m_Actors)
			f_UpdateDomainForSecretsManager(_DomainName, SecretsManager.m_Actor, SecretsManager.m_TrustInfo.m_HostInfo) > UpdateResults;

		co_await fg_AllDone(UpdateResults);

		co_return {};
	}

	CExceptionPointer CWebCertificateDeployActor::CInternal::f_UpdateDomain_CheckPreconditions(CStr const &_DomainName, CDomain *&o_pDomain, CDomainState *&o_pDomainState)
	{
		if (m_pThis->f_IsDestroyed())
			return DMibErrorInstance("Shutting down");

		o_pDomain = m_Domains.f_FindEqual(_DomainName);
		if (!o_pDomain)
			return DMibErrorInstance("Domain no longer exists");

		if (!o_pDomain->m_DomainState)
			return DMibErrorInstance("Domain no longer connected to secrets manager");

		o_pDomainState = &*o_pDomain->m_DomainState;

		return {};
	}

	CExceptionPointer CWebCertificateDeployActor::CInternal::f_UpdateDomain_CheckSecret
		(
			CSecretsManager::CSecretProperties const &_Properties
			, CSecretsManager::CSecretID const &_SecretID
			, bool _bCertificate
		)
	{
		if (!_Properties.m_Secret)
			return DMibErrorInstance("No secret found for '{}' on secrets manager"_f << _SecretID).f_ExceptionPointer();

		if (!_Properties.m_Secret->f_IsOfType<NStr::CStrSecure>())
			return DMibErrorInstance("Expected '{}' to be a binary secret"_f << _SecretID).f_ExceptionPointer();

		if (_bCertificate)
		{
			try
			{
				auto &StringData = _Properties.m_Secret->f_GetAsType<NStr::CStrSecure>();
				CByteVector CertificateData((uint8 const *)StringData.f_GetStr(), StringData.f_GetLen());

				auto IssueTime = CCertificate::fs_GetCertificateIssueTime(CertificateData);
				auto ExpirationTime = CCertificate::fs_GetCertificateExpirationTime(CertificateData);
				auto Now = CTime::fs_NowUTC();

				if (IssueTime > Now)
					return DMibErrorInstance("Certificate is not yet valid").f_ExceptionPointer();

				if (ExpirationTime < Now)
					return DMibErrorInstance("Certificate has expired").f_ExceptionPointer();
			}
			catch (CException const &_Exception)
			{
				return DMibErrorInstance("Exception checking certificate expiration time: {}"_f << _Exception).f_ExceptionPointer();
			}
		}

		return nullptr;
	}

	TCFuture<void> CWebCertificateDeployActor::CInternal::f_UpdateDomain_UpdateFiles(CStr _DomainName, CStr _CertificateType, CCertificateFilesSettings _FileSettings)
	{
		CDomain *pDomain = nullptr;
		CDomainState *pDomainState = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return f_UpdateDomain_CheckPreconditions(_DomainName, pDomain, pDomainState);
				}
			)
		;

		CSecretsManager::CSecretID PrivateKeySecretID;
		PrivateKeySecretID.m_Folder = pDomain->f_GetSecretFolder() / "Certificates" / _CertificateType;
		PrivateKeySecretID.m_Name = "PrivateKey";

		CSecretsManager::CSecretID FullChainSecretID;
		FullChainSecretID.m_Folder = pDomain->f_GetSecretFolder() / "Certificates" / _CertificateType;
		FullChainSecretID.m_Name = "FullChain";

		auto [PrivateKeySecret, FullChainSecret] = co_await
			(
				(
					pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_GetSecretProperties)(PrivateKeySecretID)
					% ("Get secret properties for {}"_f << PrivateKeySecretID)
				)
				.f_Dispatch()
				+
				(
					pDomainState->m_SecretsManager.f_CallActor(&CSecretsManager::f_GetSecretProperties)(FullChainSecretID)
					% ("Get secret properties for {}"_f << FullChainSecretID)
				)
				.f_Dispatch()
			)
		;

		if (auto pException = f_UpdateDomain_CheckSecret(PrivateKeySecret, PrivateKeySecretID, false))
			co_return pException;

		if (auto pException = f_UpdateDomain_CheckSecret(FullChainSecret, FullChainSecretID, true))
			co_return pException;

		bool bUpdated;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			bUpdated = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [_FileSettings, PrivateKeySecret = PrivateKeySecret, FullChainSecret = FullChainSecret]() -> TCFuture<bool>
					{
						TCVector<TCTuple<CStr, CStr>> ToCommit;
						bool bChanged = false;
						auto fUpdateFile = [&](CStrSecure const &_Data, CCertificateFileSettings const &_FileSettings)
							{
								CSecureByteVector FileData;
								FileData.f_Insert((uint8 const *)_Data.f_GetStr(), _Data.f_GetLen());
								CStr WriteFileName;
								if (!CFile::fs_FileExists(_FileSettings.m_Path) || !CFile::fs_FileIsSame(FileData, _FileSettings.m_Path))
								{
									WriteFileName = _FileSettings.m_Path + ".tempupdate";
									CFile::fs_WriteFileSecure(WriteFileName, FileData);
									ToCommit.f_Insert({WriteFileName, _FileSettings.m_Path});
									bChanged = true;
								}
								else
									WriteFileName = _FileSettings.m_Path;

								auto Attribs = CFile::fs_GetAttributes(WriteFileName);
								if ((Attribs & EFileAttrib_AllUnixPermissions) != (_FileSettings.m_Attributes & EFileAttrib_AllUnixPermissions))
								{
									CFile::fs_SetAttributes(WriteFileName, (_FileSettings.m_Attributes & EFileAttrib_AllUnixPermissions) | EFileAttrib_UnixAttributesValid);
									bChanged = true;
								}

								if (_FileSettings.m_Group && CFile::fs_GetGroup(WriteFileName) != _FileSettings.m_Group)
								{
									CFile::fs_SetGroup(WriteFileName, _FileSettings.m_Group);
									bChanged = true;
								}

								if (_FileSettings.m_User && CFile::fs_GetOwner(WriteFileName) != _FileSettings.m_User)
								{
									CFile::fs_SetOwner(WriteFileName, _FileSettings.m_User);
									bChanged = true;
								}
							}
						;

						{
							auto CaptureScope = co_await g_CaptureExceptions;

							fUpdateFile(PrivateKeySecret.m_Secret->f_GetAsType<NStr::CStrSecure>(), _FileSettings.m_Key);
							fUpdateFile(FullChainSecret.m_Secret->f_GetAsType<NStr::CStrSecure>(), _FileSettings.m_FullChain);

							for (auto &ToCommit : ToCommit)
								CFile::fs_AtomicReplaceFile(fg_Get<0>(ToCommit), fg_Get<1>(ToCommit));
						}

						co_return bChanged;
					}
				)
			;
		}

		if (bUpdated && pDomain->m_Settings.m_fOnCertificateUpdated)
			co_await pDomain->m_Settings.m_fOnCertificateUpdated(_DomainName, _CertificateType == "RSA" ? ECertificate_Rsa : ECertificate_Ec);

		co_return {};
	}

	TCFuture<void> CWebCertificateDeployActor::CInternal::f_UpdateDomain(CStr _DomainName)
	{
		CDomain *pDomain = nullptr;
		CDomainState *pDomainState = nullptr;

		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return f_UpdateDomain_CheckPreconditions(_DomainName, pDomain, pDomainState);
				}
			)
		;

		f_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Info, "Secrets manager connected, updating files");

		TCFutureVector<void> UpdateFilesResults;

		if (pDomain->m_Settings.m_FileSettings_Ec)
			f_UpdateDomain_UpdateFiles(_DomainName, CStr("EC"), *pDomain->m_Settings.m_FileSettings_Ec) > UpdateFilesResults;

		if (pDomain->m_Settings.m_FileSettings_Rsa)
			f_UpdateDomain_UpdateFiles(_DomainName, CStr("RSA"), *pDomain->m_Settings.m_FileSettings_Rsa) > UpdateFilesResults;

		co_await fg_AllDone(UpdateFilesResults);

		f_UpdateDomainStatus(*pDomain, pDomainState->m_SecretsManagerHostInfo, EStatusSeverity_Success, "All certificates deployed and up to date");

		co_return {};
	}
}
