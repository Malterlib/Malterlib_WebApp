// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
		if (mp_Options.m_Mongo.m_DatabaseSetupScript.f_IsEmpty())
			co_return {};

		co_await fg_CallSafe
			(
			 	this
			 	, &CWebAppManagerActor::fp_RunMongoScript
				, fg_Format("{}/Source/{}", CFile::fs_GetProgramDirectory(), mp_Options.m_Mongo.m_DatabaseSetupScript)
				, mp_MongoDatabase
				, 120.0
			)
		;

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Mongo()
	{
		struct CSetupResult
		{
			CStr m_MongoAdminUserName;
		};

		mp_MongoCertificateDeployActor = fg_Construct(mp_AppState.m_DistributionManager, mp_AppState.m_TrustManager, *mp_FileActors);

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

			UserSettings.m_fOnStatusChange = g_ActorFunctor / [](CHostInfo &&_HostInfo, CMongoCertificateDeployActor::CUserStatus &&_Status) -> TCFuture<void>
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

		auto SetupResult = co_await
			(
				mp_FileActors.f_Dispatch() / [=]() -> CSetupResult
				{
					CSetupResult SetupResult;

					if (!MongoSSLDirectory.f_IsEmpty())
					{
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
		else if (mp_bConnectToExternalMongo)
			return CFile::fs_GetProgramDirectory() / "mongo/certificates";

		return MongoSSLDirectory;
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

	TCFuture<void> CWebAppManagerActor::fp_RunMongoScript(CStr const &_Script, CStr const &_Database, fp32 _Timeout)
	{
		TCPromise<void> Promise;

		CStr ScriptName = CFile::fs_GetFile(_Script);
		CStr MongoSSLDirectory = fp_GetMongoSSLDirectory();

		CProcessLaunchParams Params;
		fs_SetupEnvironment(Params);
		Params.m_bAllowExecutableLocate = true;
		Params.m_bMergeEnvironment = true;
		Params.m_RunAsUser = mp_MongoToolsUser;
#ifdef DPlatformFamily_Windows
		if (mp_MongoToolsUser)
			Params.m_RunAsUser = fp_GetUserPassword(mp_MongoToolsUser);
#endif
		Params.m_RunAsGroup = mp_MongoToolsGroup;

		CStr MongoHost = mp_MongoHost;
		int64 MongoPort = mp_MongoPort;

		CStr Address = fp_GetDBAddress(_Database, MongoSSLDirectory);

		if (mp_bConnectToExternalMongo)
			MongoHost = CMongoConnectionSettings::fs_GetConnectionString(mp_ExternalMongoHosts);

		if (MongoHost.f_IsEmpty())
			co_return DMibErrorInstance(fg_Format("Failed to launch mongo for running {}: Hostname is empty", ScriptName));

		TCVector<CStr> CommandLineArgs;

		if (!MongoSSLDirectory.f_IsEmpty())
		{
			CStr CaCertificatePath = MongoSSLDirectory + "/MongoCA.crt";
			CStr ClientCertificatePath = MongoSSLDirectory + "/admin.pem";

			CommandLineArgs.f_Insert
				(
					{
						"--tls"
						, "--tlsCertificateKeyFile"
						, ClientCertificatePath
					}
				)
			;

			if (CFile::fs_FileExists(CaCertificatePath))
			{
				CommandLineArgs.f_Insert
					(
						{
							"--tlsCAFile"
							, CaCertificatePath
						}
					)
				;
			}
		}

		if (!mp_bConnectToExternalMongo)
		{
			CommandLineArgs << fg_CreateVector<CStr>
				(
					"--eval"
					, fg_Format
					(
						"var WebAppManagerMongoHostName='{}'; var WebAppManagerMongoPort='{}'"
						, MongoHost
						, MongoPort
					)
				)
			;
		}

		CommandLineArgs << fg_CreateVector<CStr>
			(
				"--quiet"
				, Address
				, _Script
			)
		;

		CStr MongoExecutable = fp_GetMongoExecutable("mongo");

		auto Clock = CClock{true};

		while (true)
		{
			auto StdOutResult = co_await f_LaunchTool
				(
					MongoExecutable
					, CFile::fs_GetPath(MongoExecutable)
					, CommandLineArgs
					, ScriptName
					, ELogVerbosity_None
					, {}
					, true
				)
				.f_Wrap()
			;

			if (!StdOutResult)
			{
				if
					(
						(
							StdOutResult.f_GetExceptionStr().f_Find("exception: connect failed") >= 0
							|| StdOutResult.f_GetExceptionStr().f_Find("The OS returned an error from execve") >= 0
							|| StdOutResult.f_GetExceptionStr().f_Find("not master and slaveOk=false") >= 0
						)
						&& _Timeout != 0.0f
						&& Clock.f_GetTime() < _Timeout
					)
				{
					co_await fg_Timeout(0.1);
					continue;
				}
				co_return StdOutResult.f_GetException();
			}

			auto StdOut = (*StdOutResult).f_Trim();
			DLog(Info, "{}:{}{}", ScriptName, StdOut.f_IsEmpty() ? "" : "\n", StdOut);
			break;
		}

		co_return {};
	}
}
