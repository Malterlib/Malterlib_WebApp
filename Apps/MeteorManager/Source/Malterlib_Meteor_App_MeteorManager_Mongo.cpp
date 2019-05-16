// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Cryptography/Certificate>
#include <Mib/Concurrency/Actor/Timer>

namespace NMib::NMeteor::NMeteorManager
{
	CStr CMeteorManagerActor::fp_GetMongoExecutable(CStr const &_ExecutableName) const
	{
		if (!mp_MongoDirectory.f_IsEmpty())
			return CFile::fs_AppendPath(CFile::fs_GetExpandedPath(mp_MongoDirectory, CFile::fs_GetProgramDirectory()), _ExecutableName);
		return _ExecutableName;
	}
	
	TCFuture<void> CMeteorManagerActor::fp_SetupMongo()
	{
		if (mp_Options.m_Mongo.m_DatabaseSetupScript.f_IsEmpty())
			return fg_Explicit();
		
		return fp_RunMongoScript
			(
				fg_Format("{}/Source/{}", CFile::fs_GetProgramDirectory(), mp_Options.m_Mongo.m_DatabaseSetupScript)
				, mp_MongoDatabase
				, 120.0
			)
		;
	}

	TCFuture<void> CMeteorManagerActor::fp_SetupPrerequisites_Mongo()
	{
		TCPromise<void> Promise;

		struct CSetupResult
		{
			CStr m_MongoAdminUserName;
		};

		g_Dispatch(*mp_FileActors) / [=, MongoSSLDirectory = fp_GetMongoSSLDirectory()]() -> CSetupResult
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
			> Promise / [=](CSetupResult &&_SetupResult)
			{
				mp_MongoAdminUserName = _SetupResult.m_MongoAdminUserName;
				Promise.f_SetResult();
			}
		;

		return Promise.f_MoveFuture();
	}
	
	CStr CMeteorManagerActor::fp_GetMongoSSLDirectory() const
	{
		CStr MongoSSLDirectory = mp_MongoSSLDirectory;

		if (!MongoSSLDirectory.f_IsEmpty())
			MongoSSLDirectory = CFile::fs_GetExpandedPath(MongoSSLDirectory, CFile::fs_GetProgramDirectory());
		
		return MongoSSLDirectory;
	}

	TCFuture<void> CMeteorManagerActor::fp_RunMongoScript(CStr const &_Script, CStr const &_Database, fp32 _Timeout)
	{
		CStr ScriptName = CFile::fs_GetFile(_Script);

		CProcessLaunchParams Params;
		fs_SetupEnvironment(Params);
		Params.m_bAllowExecutableLocate = true;
		Params.m_bMergeEnvironment = true;
		Params.m_RunAsUser = mp_MongoToolsUser;
#ifdef DPlatformFamily_Windows
		Params.m_RunAsUser = fp_GetUserPassword(mp_MongoToolsUser);
#endif
		Params.m_RunAsGroup = mp_MongoToolsGroup;

		CStr MongoHost = mp_MongoHost;
		int64 MongoPort = mp_MongoPort;

		if (MongoHost.f_IsEmpty())
			return DMibErrorInstance(fg_Format("Failed to launch mongo for running {}: {}", ScriptName, "Hostname is empty"));
		
		TCVector<CStr> CommandLineArgs = fg_CreateVector<CStr>
			(
				"--host"
				, MongoHost
				, "--port"
				, CStr::fs_ToStr(MongoPort)
			)
		;
		
		CStr MongoSSLDirectory = fp_GetMongoSSLDirectory();

		if (!MongoSSLDirectory.f_IsEmpty())
		{
			CStr CACertificatePath = MongoSSLDirectory + "/MongoCA.crt";
			CStr ClientCertificatePath = MongoSSLDirectory + "/admin.pem";

			CommandLineArgs << fg_CreateVector<NStr::CStr>
				(
					"--ssl"
					, "--authenticationMechanism"
					, "MONGODB-X509"
					, "--authenticationDatabase"
					, "$external"
					, "--sslCAFile"
					, CACertificatePath
					, "--sslPEMKeyFile"
					, ClientCertificatePath
					, "-u"
					, mp_MongoAdminUserName
				)
			;
		}

		CommandLineArgs << fg_CreateVector<CStr>
			(
				"--quiet"
				, "--eval"
				, fg_Format
				(
					"var MeteorManagerMongoHostName='{}'; var MeteorManagerMongoPort='{}'"
					, MongoHost
					, MongoPort
				)
				, _Database
				, _Script
			)
		;
		
		CStr MongoExecutable = fp_GetMongoExecutable("mongo");
		
		TCPromise<void> Promise;
		
		TCSharedPointer<TCFunctionMutable<void (TCPromise<void> const &_Promise)>> pDoLaunch = fg_Construct();
		
		*pDoLaunch =
			[
				Clock = CClock{true}
				, MongoExecutable
				, CommandLineArgs
				, ScriptName
				, this
				, _Timeout
				, pDoLaunch
			]
			(TCPromise<void> const &_Promise) mutable
			{
				if (_Promise.f_IsSet())
				{
					pDoLaunch.f_Clear();
					return;
				}
				f_LaunchTool
					(
						MongoExecutable
						, CFile::fs_GetPath(MongoExecutable)
						, CommandLineArgs
						, ScriptName
						, ELogVerbosity_None
						, {}
						, true
					)
					> [ScriptName, _Promise, Clock, _Timeout, pDoLaunch](TCAsyncResult<CStr> const &_StdOut)
					{
						if (!_StdOut)
						{
							if (_StdOut.f_GetExceptionStr().f_Find("exception: connect failed") >= 0 && _Timeout != 0.0f && Clock.f_GetTime() < _Timeout)
							{
								fg_Timeout(0.1) > _Promise / [pDoLaunch, _Promise]
									{
										(*pDoLaunch)(_Promise);
									}
								;
								return;
							}
							_Promise.f_SetException(_StdOut);
							(*pDoLaunch)(_Promise);
							return;
						}
						auto StdOut = (*_StdOut).f_Trim();
						DLog(Info, "{}:{}{}", ScriptName, StdOut.f_IsEmpty() ? "" : "\n", StdOut);
						_Promise.f_SetResult();
						(*pDoLaunch)(_Promise);
					}
				;
			}
		;
		
		(*pDoLaunch)(Promise);
		
		return Promise.f_MoveFuture();
	}
}
