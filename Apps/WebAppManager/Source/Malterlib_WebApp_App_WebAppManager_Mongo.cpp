// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/Cryptography/Certificate>
#include <Mib/Concurrency/Actor/Timer>

namespace NMib::NWebApp::NWebAppManager
{
	CStr CWebAppManagerActor::fp_GetMongoExecutable(CStr const &_ExecutableName) const
	{
		if (!mp_MongoDirectory.f_IsEmpty())
			return CFile::fs_GetExpandedPath(mp_MongoDirectory, CFile::fs_GetProgramDirectory()) / mp_MongoVersion / "bin" / _ExecutableName;
		return _ExecutableName;
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupMongo()
	{
		if (!mp_Options.m_bNeedsMongo)
			co_return {};

		if (mp_Options.m_Mongo.m_DatabaseSetupPackage.f_IsEmpty())
			co_return {};

		auto *pPackage = mp_Options.m_Packages.f_FindEqual(mp_Options.m_Mongo.m_DatabaseSetupPackage);
		if (!pPackage)
			co_return DMibErrorInstance("WebAppManagerMongoDatabaseSetupPackage '{}' was not found in packages"_f << mp_Options.m_Mongo.m_DatabaseSetupPackage);

		co_await fp_RunNodeMongoScript
			(
				"DatabaseSetup"
				, fp_GetPackageRoot(mp_Options.m_Mongo.m_DatabaseSetupPackage) / pPackage->m_MainFile
				, pPackage->m_CustomParams
				, mp_MongoDatabase
				, 120.0
			)
		;

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Mongo()
	{
		if (!mp_Options.m_bNeedsMongo)
			co_return {};

		struct CSetupResult
		{
			CStr m_MongoAdminUserName;
		};

		mp_MongoCertificateDeployActor = fg_Construct(mp_AppState.m_DistributionManager, mp_AppState.m_TrustManager);

		CStr CertificateAuthority = mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue("MongoCertificateAuthority", "MongoCA").f_String();
		auto MongoSSLDirectory = fp_GetMongoSSLDirectory();
		{
			CMongoCertificateDeployActor::CUserSettings UserSettings;
			UserSettings.f_InitUser
				(
					CertificateAuthority
					, "admin"
					,
					{
						{
							.m_BasePath = MongoSSLDirectory
							, .m_FileUser = mp_MongoToolsUser
							, .m_FileGroup = mp_MongoToolsGroup
						}
					}
				)
			;

			UserSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo _HostInfo, CMongoCertificateDeployActor::CUserStatus _Status) -> TCFuture<void>
				{
					if (_Status.m_Severity == CMongoCertificateDeployActor::EStatusSeverity_Error)
						DMibLogWithCategory(Certificate, Error, "Mongo admin certificate: {}", _Status.m_Description);
					else if (_Status.m_Severity == CMongoCertificateDeployActor::EStatusSeverity_Warning)
						DMibLogWithCategory(Certificate, Warning, "Mongo admin certificate: {}", _Status.m_Description);
					else
						DMibLogWithCategory(Certificate, Info, "Mongo admin certificate: {}", _Status.m_Description);

					co_return {};
				}
			;

			mp_MongoCertificateDeploySubscription_Admin = co_await mp_MongoCertificateDeployActor(&CMongoCertificateDeployActor::f_AddUser, fg_Move(UserSettings));
		}

		co_await mp_MongoCertificateDeployActor(&CMongoCertificateDeployActor::f_Start);

		auto BlockingActorCheckout = fg_BlockingActor();
		auto SetupResult = co_await
			(
				g_Dispatch(BlockingActorCheckout) / [=, bConnectToExternalMongo = mp_bConnectToExternalMongo]() -> CSetupResult
				{
					CSetupResult SetupResult;

					if (MongoSSLDirectory.f_IsEmpty() || !bConnectToExternalMongo)
						return SetupResult;

					if (CFile::fs_FileExists(MongoSSLDirectory) && MongoSSLDirectory.f_StartsWith(CFile::fs_GetProgramDirectory() + "/"))
					{
						CFile::fs_SetUnixAttributesRecursive
							(
								MongoSSLDirectory
								, EFileAttrib_UserRead
								, EFileAttrib_UserRead | EFileAttrib_UserExecute
							)
						;
					}

					CStr ClientCertificatePath = MongoSSLDirectory / "admin.pem";

					try
					{
						SetupResult.m_MongoAdminUserName = CCertificate::fs_GetCertificateDistinguishedName_RFC2253(CFile::fs_ReadFile(ClientCertificatePath));
					}
					catch (CException const &_Error)
					{
						DMibError("Failed to read certificate file for admin user: {}"_f << _Error);
					}

					return SetupResult;
				}
			)
		;

		mp_MongoAdminUserName = SetupResult.m_MongoAdminUserName;

		co_return {};
	}

	CStr CWebAppManagerActor::fp_GetMongoSSLDirectory() const
	{
		CStr MongoSSLDirectory = mp_MongoSSLDirectory;

		if (!MongoSSLDirectory.f_IsEmpty())
			return CFile::fs_GetExpandedPath(MongoSSLDirectory, CFile::fs_GetProgramDirectory());

		return CFile::fs_GetProgramDirectory() / "mongo/certificates";
	}

	NWeb::NHTTP::CURL CWebAppManagerActor::fp_GetDBAddressURL(CStr _Database, CStr _HomePath)
	{
		if (mp_bConnectToExternalMongo)
		{
			CStr CaCertificatePath = _HomePath / "MongoCA.crt";
			CStr ClientCertificatePath = _HomePath / "admin.pem";

			NWeb::NHTTP::CURL Url;
			Url.f_SetScheme("mongodb");
			Url.f_SetUsername(mp_MongoAdminUserName);
			Url.f_SetHost(CMongoConnectionSettings::fs_GetConnectionString(mp_ExternalMongoHosts), true);
			Url.f_SetPath({_Database});

			NContainer::TCVector<NWeb::NHTTP::CURL::CQueryEntry> Query
				{
					{
						{"authMechanism", "MONGODB-X509"}
						, {"retryWrites", "true"}
						, {"w", "majority"}
						, {"authSource", "$external"}
						, {"tls", "true"}
						, {"tlsCertificateKeyFile", ClientCertificatePath}
					}
				}
			;

			Query.f_Insert({"replicaSet", mp_MongoReplicaNameExternal});

			if (CFile::fs_FileExists(CaCertificatePath))
				Query.f_Insert({"tlsCAFile", CaCertificatePath});

			Url.f_SetQuery(Query);

			return Url;
		}
		else
		{
			NWeb::NHTTP::CURL Url;
			Url.f_SetScheme("mongodb");
			Url.f_SetHost(mp_MongoHost);
			Url.f_SetPort(mp_MongoPort);
			Url.f_SetPath({_Database});

			return Url;
		}
	}

	CStr CWebAppManagerActor::fp_GetDBAddress(CStr _Database, CStr _HomePath)
	{
		return fp_GetDBAddressURL(_Database, _HomePath).f_Encode();
	}

	TCFuture<void> CWebAppManagerActor::fp_RunNodeMongoScript(CStr _ScriptName, CStr _Script, TCVector<CStr> _Params, CStr _Database, fp32 _Timeout)
	{
		CStr MongoSSLDirectory = fp_GetMongoSSLDirectory();

		CStr Address = fp_GetDBAddress(_Database, MongoSSLDirectory);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("--max-old-space-size=3000");
		Arguments.f_Insert("--trace-deprecation");
		Arguments.f_Insert("--trace-warnings");
		Arguments.f_Insert(_Script);
		Arguments.f_Insert(_Params);

		TCMap<CStr, CStr> Environment;

		Environment["MONGO_URL"] = Address;

		CStopwatch Stopwatch{true};

		while (true)
		{
			auto StdOutResult = co_await f_LaunchTool
				(
					fp_GetNodeExecutable("node", false)
					, CFile::fs_GetPath(_Script)
					, Arguments
					, _ScriptName
					, ELogVerbosity_None
					, Environment
					, true
					, {}
					, mp_MongoToolsUser
					, mp_MongoToolsGroup
				)
				.f_Wrap()
			;

			if (!StdOutResult)
			{
				if
					(
						(
							StdOutResult.f_GetExceptionStr().f_Find("MongoNetworkError: connect") >= 0
							|| StdOutResult.f_GetExceptionStr().f_Find("MongoPoolClearedError:") >= 0
							|| StdOutResult.f_GetExceptionStr().f_Find("The OS returned an error from execve") >= 0
							|| StdOutResult.f_GetExceptionStr().f_Find("not master and slaveOk=false") >= 0
						)
						&& _Timeout != 0.0f
						&& Stopwatch.f_GetTime() < _Timeout
					)
				{
					co_await fg_Timeout(0.1);
					continue;
				}
				co_return StdOutResult.f_GetException();
			}

			auto StdOut = (*StdOutResult).f_Trim();
			DLog(Info, "{}:{}{}", _ScriptName, StdOut.f_IsEmpty() ? "" : "\n", StdOut);
			break;
		}

		co_return {};
	}
}
