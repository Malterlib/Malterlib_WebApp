// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Network/SSL>
#include <Mib/Concurrency/Actor/Timer>

namespace NMib::NMeteor::NMeteorManager
{
	CStr CMeteorManagerActor::fp_GetMongoExecutable(CStr const &_ExecutableName) const
	{
		auto MongoDirectory = fp_GetConfigValue("MongoDirectory", {});
		if (!MongoDirectory.f_String().f_IsEmpty())
			return CFile::fs_AppendPath(CFile::fs_GetExpandedPath(MongoDirectory.f_String(), CFile::fs_GetProgramDirectory()), _ExecutableName);
		return _ExecutableName;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_SetupMongo()
	{
		if (mp_Options.m_Mongo.m_DatabaseSetupScript.f_IsEmpty())
			return fg_Explicit();
		
		return fp_RunMongoScript
			(
				fg_Format("{}/Source/{}", CFile::fs_GetProgramDirectory(), mp_Options.m_Mongo.m_DatabaseSetupScript)
				, fp_GetConfigValue("MongoDefaultDatabase", {}).f_String()
				, 120.0
			)
		;
	}
	
	CStr CMeteorManagerActor::fp_GetMongoSSLDirectory() const
	{
		CStr MongoSSLDirectory = fp_GetConfigValue("MongoSSLDirectory", {}).f_String();

		if (!MongoSSLDirectory.f_IsEmpty())
			MongoSSLDirectory = CFile::fs_GetExpandedPath(MongoSSLDirectory, CFile::fs_GetProgramDirectory());
		
		return MongoSSLDirectory;
	}

	TCContinuation<void> CMeteorManagerActor::fp_RunMongoScript(CStr const &_Script, CStr const &_Database, fp32 _Timeout)
	{
		CStr ScriptName = CFile::fs_GetFile(_Script);

		CProcessLaunchParams Params;
		fs_SetupEnvironment(Params);
		Params.m_bAllowExecutableLocate = true;
		Params.m_bMergeEnvironment = true;
		Params.m_RunAsUser = fp_GetConfigValue("MongoToolsUser", {}).f_String();
		Params.m_RunAsGroup = fp_GetConfigValue("MongoToolsGroup", {}).f_String();

		CStr MongoHost = fp_GetConfigValue("MongoHost", {}).f_String();
		int64 MongoPort = fp_GetConfigValue("MongoPort", {}).f_Integer();

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
			CStr UserName;

			try
			{
				UserName = CSSLContext::fs_GetCertificateDistinguishedName_RFC2253(CFile::fs_ReadFile(ClientCertificatePath));
			}
			catch (CException const &_Error)
			{
				return DMibErrorInstance(fg_Format("Failed to read certificate file: {}", _Error));
			}

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
					, UserName
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
		
		TCContinuation<void> Continuation;
		
		TCSharedPointer<TCFunctionMutable<void (TCContinuation<void> const &_Continuation)>> pDoLaunch = fg_Construct();
		
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
			(TCContinuation<void> const &_Continuation) mutable
			{
				f_LaunchTool
					(
						MongoExecutable
						, CFile::fs_GetPath(MongoExecutable)
						, CommandLineArgs
						, ScriptName
						, ELogVerbosity_None
						, {}
						, true
						, {}
						, {}
					)
					> [ScriptName, _Continuation, Clock, _Timeout, pDoLaunch = fg_Move(pDoLaunch)](TCAsyncResult<CStr> const &_StdOut)
					{
						if (!_StdOut)
						{
							if (_StdOut.f_GetExceptionStr().f_Find("exception: connect failed") >= 0 && _Timeout != 0.0f && Clock.f_GetTime() < _Timeout)
							{
								fg_Timeout(0.1) > _Continuation / [pDoLaunch, _Continuation]
									{
										(*pDoLaunch)(_Continuation);
									}
								;
								return;
							}
							_Continuation.f_SetException(_StdOut);
							return;
						}
						DLog(Info, "{}:\n{}", ScriptName, (*_StdOut).f_Trim());
						_Continuation.f_SetResult();
					}
				;
			}
		;
		
		(*pDoLaunch)(Continuation);
		
		return Continuation;
	}
}
