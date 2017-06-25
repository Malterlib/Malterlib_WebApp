// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Network/SSL>

namespace NMib::NMeteor::NMeteorManager
{
	mint CMeteorManagerActor::fs_GetNodeFileLimits()
	{
		return 65536 + 8192;
	}
	
	CStr CMeteorManagerActor::fp_GetNodeExecutable(CStr const &_Executable)
	{
		if (mp_Options.m_bUseInternalNode)
			return CFile::fs_GetProgramDirectory() + "/node_dist/bin/node";
		else
			return "node";
	}
	
	CStr CMeteorManagerActor::fp_GetPackageHostname(CStr const &_PackageName) const
	{
		auto &Package = mp_Options.m_Packages[_PackageName];
		if (Package.m_Type != CMeteorManagerOptions::EPackageType_Meteor)
			DMibError("Cannot get package name for non-meteor package");
		
		CStr Hostname;
		if (!Package.m_DomainPrefix.f_IsEmpty())
			Hostname = fg_Format("{}.{}", Package.m_DomainPrefix, mp_Domain);
		else
			Hostname = mp_Domain;
		
		return Hostname;
	}
	
	CStr CMeteorManagerActor::fp_GetRootURL(CStr const &_Hostname) const
	{
		if (mp_WebSSLPort == 443)
			return fg_Format("https://{}/", _Hostname);
		else
			return fg_Format("https://{}:{}/", _Hostname, mp_WebSSLPort);
	}

	CStr CMeteorManagerActor::fp_GetAppIPAddress(CAppLaunch const &_AppLaunch) const
	{
		return fg_Format("127.{}.{}.1", mp_Options.m_LoopbackPrefix, 2 + _AppLaunch.f_GetKey().m_iAppSequence);
	}

	CStr CMeteorManagerActor::fp_GetAppLocalURL(CAppLaunch const &_AppLaunch) const
	{
		return fg_Format("http://{}:8080/", fp_GetAppIPAddress(_AppLaunch));
	}

	CStr CMeteorManagerActor::fp_GetPackageLocalURL(CStr const &_PackageName) const
	{
		auto &CurrentURL = mp_CurrentPackageLocalURL[_PackageName];
		mint iURL = CurrentURL;
		
		auto &Package = mp_Options.m_Packages[_PackageName];
		++CurrentURL;
		if (CurrentURL >= Package.m_Concurrency)
			CurrentURL = 0;
		
		return mp_PackageLocalURLs[_PackageName][iURL];
	}

	void CMeteorManagerActor::fp_CreateAppLaunches()
	{
		for (auto &Package : mp_Options.m_Packages)
		{
			if (Package.m_Type == CMeteorManagerOptions::EPackageType_Custom && Package.m_CustomExecutable.f_IsEmpty())
				continue;

			CAppLaunchKey LaunchKey;
			LaunchKey.m_PackageName = Package.f_GetName();
			for (mint i = 0; i < Package.m_Concurrency; ++i)
			{
				LaunchKey.m_iAppSequence = mp_AppSequence++;
				auto &AppLaunch = mp_AppLaunches[LaunchKey];
				++mp_OutstandingLaunches[Package.f_GetName()];
				if (Package.m_Concurrency > 1)
					AppLaunch.m_LogCategory = fg_Format("{}-{}", LaunchKey.m_PackageName, i);
				else
					AppLaunch.m_LogCategory = LaunchKey.m_PackageName;
				
				AppLaunch.m_BackendIdentifier = fg_Format("{}_{}", LaunchKey.m_PackageName, fg_RandomID());
				
				mp_PackageLocalURLs[LaunchKey.m_PackageName].f_Insert(fp_GetAppLocalURL(AppLaunch));
			}
		}
	}

	TCContinuation<void> CMeteorManagerActor::fp_StartApps()
	{
		fp_UpdateAppLaunch(nullptr);
		return mp_AppLaunchesContinuation;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_DestroyApps()
	{
		TCActorResultVector<void> Destroys;
		
		for (auto &Launch : mp_AppLaunches)
		{
			if (!Launch.m_Launch)
				continue;
			Launch.m_Launch->f_Destroy() > Destroys.f_AddResult();
		}
	
		TCContinuation<void> Continuation;
		Destroys.f_GetResults() > Continuation.f_ReceiveAny();
		return Continuation;
	}
	
	void CMeteorManagerActor::fp_UpdateAppLaunch(CExceptionPointer const &_pException)
	{
		if (_pException)
		{
			if (!mp_AppLaunchesContinuation.f_IsSet())
				mp_AppLaunchesContinuation.f_SetException(_pException);
			return;
		}
		
		for (auto &Launch : mp_AppLaunches)
		{
			auto &LaunchKey = Launch.f_GetKey();
			auto &PackageOptions = fg_Const(mp_Options.m_Packages)[LaunchKey.m_PackageName];
			bool bPrevented = false;
			for (auto &Dependency : PackageOptions.m_StartupDependencies)
			{
				auto *pOutstanding = mp_OutstandingLaunches.f_FindEqual(Dependency);
				if (!pOutstanding)
				{
					if (!mp_AppLaunchesContinuation.f_IsSet())
					{
						mp_AppLaunchesContinuation.f_SetException
							(
								DErrorInstance(fg_Format("No such package '{}' when trying to wait for dependencies for '{}'", Dependency, LaunchKey.m_PackageName))
							)
						;
					}
					return;
				}
				if (*pOutstanding)
				{
					bPrevented = true;
				}
			}
			if (bPrevented)
				continue;
			if (Launch.m_bInitialLaunched)
				continue;
			Launch.m_bInitialLaunched = true;
			
			fp_LaunchApp(Launch, true);
		}
		
		bool bAllDone = true;
		for (auto &Outstanding : mp_OutstandingLaunches)
		{
			if (Outstanding)
			{
				bAllDone = false;
				break;
			}
		}

		if (bAllDone && !mp_AppLaunchesContinuation.f_IsSet())
			mp_AppLaunchesContinuation.f_SetResult();
	}
	
	void CMeteorManagerActor::fp_SetupNodeArguments(TCVector<CStr> &o_Arguments, CAppLaunch const &_AppLaunch, CMeteorManagerOptions::CPackage const &_PackageOptions)
	{
		o_Arguments.f_Insert(fg_Format("--max_old_space_size={}", (_PackageOptions.m_MemoryPerNode*1024.0).f_ToInt()));
		
		if (fp_GetConfigValue("NodeDebug", false).f_Boolean())
			o_Arguments.f_Insert(fg_Format("--debug={}", 7000 + mp_Options.m_LoopbackPrefix*1000 + _AppLaunch.f_GetKey().m_iAppSequence));

		o_Arguments.f_Insert(_PackageOptions.m_CustomParams);
	}

	void CMeteorManagerActor::fp_PopulateNodeEnvironment
		(
			CSystemEnvironment &o_Environment
			, TCMap<CStr, CStr> const &_CalculatedSettings
			, CAppLaunch const &_AppLaunch
			, CMeteorManagerOptions::CPackage const &_PackageOptions
		)
	{
		TCSet<CStr> Tags = mp_Tags;
		Tags[_PackageOptions.f_GetName()];
		Tags[fg_Format("{}_{}", _PackageOptions.f_GetName(), _AppLaunch.f_GetKey().m_iAppSequence)];
		
		switch(_PackageOptions.m_Type)
		{
		case CMeteorManagerOptions::EPackageType_Meteor: Tags["Meteor"]; break;
		case CMeteorManagerOptions::EPackageType_Npm: Tags["Npm"]; break;
		case CMeteorManagerOptions::EPackageType_Custom: Tags["Custom"]; break;
		}
		
		for (auto &EnvVar : mp_Options.m_Environment)
		{
			bool bPassAllTags = true;

			for (auto &Tags : EnvVar.m_RequiredTags)
			{
				bool bFoundOne = false;
				for (auto &Tag : Tags)
				{
					if (Tags.f_FindEqual(Tag))
						bFoundOne = true;
				}
				if (!bFoundOne)
					bPassAllTags = false;
			}

			for (auto &Tag : EnvVar.m_ForbiddenTags)
			{
				if (Tags.f_FindEqual(Tag))
					bPassAllTags = false;
			}

			if (!bPassAllTags)
				continue;
			
			if (auto *pSetting = _CalculatedSettings.f_FindEqual(EnvVar.m_Setting))
			{
				o_Environment[EnvVar.f_GetName()] = *pSetting;
				continue;
			}
			
			CEJSON Value;
			if (EnvVar.m_Default)
				Value = fp_GetConfigValue(EnvVar.m_Setting, *EnvVar.m_Default);
			else
				Value = fp_GetConfigValue(EnvVar.m_Setting, nullptr);
			
			if (Value.f_IsNull())
				continue;
			
			o_Environment[EnvVar.f_GetName()] = Value.f_AsString();
		}
	}
	
	void CMeteorManagerActor::fp_LaunchApp(CAppLaunch &_AppLaunch, bool _bInitialLaunch)
	{
		if (_AppLaunch.m_Launch || mp_bStopped || mp_bDestroyed)
			return; // Launch already in progress
		
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		
		TCVector<CStr> Arguments;
		TCContinuation<void> Continuation;
		
		auto *pAppLaunch = &_AppLaunch;
		auto &LaunchKey = _AppLaunch.f_GetKey();
		
		CStr LaunchExecutable;
		CStr PackageDirectory = fg_Format("{}/{}", ProgramDirectory, _AppLaunch.f_GetKey().m_PackageName);

		auto &PackageOptions = fg_Const(mp_Options.m_Packages)[LaunchKey.m_PackageName];
		
		if (PackageOptions.m_Type == CMeteorManagerOptions::EPackageType_Meteor)
		{
			LaunchExecutable = ProgramDirectory + "/node_dist/bin/node";
			
			fp_SetupNodeArguments(Arguments, _AppLaunch, PackageOptions);
			
			Arguments.f_Insert(fg_Format("{}/{}/main.js", ProgramDirectory, LaunchKey.m_PackageName));
		}
		else if (PackageOptions.m_Type == CMeteorManagerOptions::EPackageType_Npm)
		{
			LaunchExecutable = ProgramDirectory + "/node_dist/bin/node";

			fp_SetupNodeArguments(Arguments, _AppLaunch, PackageOptions);
			
			Arguments.f_Insert(fg_Format("{}/{}/bin/main.js", ProgramDirectory, LaunchKey.m_PackageName));
		}
		else if (PackageOptions.m_Type == CMeteorManagerOptions::EPackageType_Custom)
		{
			if (PackageOptions.m_CustomExecutable.f_IsEmpty())
			{
				if (_bInitialLaunch)
					fp_UpdateAppLaunch(fg_ExceptionPointer(DMibErrorInstance(fg_Format("Missing executable for custom launch: {}", LaunchKey.m_PackageName))));
				return;
			}
			LaunchExecutable = PackageOptions.m_CustomExecutable;
			Arguments = PackageOptions.m_CustomParams;
		}
		else
		{
			if (_bInitialLaunch)
				fp_UpdateAppLaunch(fg_ExceptionPointer(DMibErrorInstance("Invalid package type")));
			return;
		}
		
		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				LaunchExecutable
				, Arguments
				, PackageDirectory
				, [this, Continuation, pAppLaunch, _bInitialLaunch](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
				{
					auto &AppLaunch = *pAppLaunch;
					
					DLogCategoryStr(AppLaunch.m_LogCategory);
					
					switch (_Change.f_GetTypeID())
					{
					case EProcessLaunchState_Launched:
						{
							if (mp_pCanDestroyTracker.f_IsEmpty() || mp_bStopped)
							{
								if (AppLaunch.m_Launch)
									AppLaunch.m_Launch(&CProcessLaunchActor::f_StopProcess) > fg_DiscardResult();
								if (_bInitialLaunch)
									fp_UpdateAppLaunch(fg_ExceptionPointer(DMibErrorInstance("Application is being destroyed")));
							}
							else
							{
								if (_bInitialLaunch)
								{
									--mp_OutstandingLaunches[pAppLaunch->f_GetKey().m_PackageName];
									fp_UpdateAppLaunch(nullptr);
								}
							}
						}
						break;
					case EProcessLaunchState_Exited:
						{
							if (!mp_pCanDestroyTracker.f_IsEmpty() && !mp_bStopped)
							{
								if (_bInitialLaunch)
									fp_UpdateAppLaunch(fg_ExceptionPointer(DMibErrorInstance(fg_Format("Unexpected exit {}", _Change.f_Get<EProcessLaunchState_Exited>()))));
								
								DLog(Info, "Unexpected exit {}, scheduling relaunch in 10 seconds", _Change.f_Get<EProcessLaunchState_Exited>());
								fg_Timeout(10.0) > [this, pAppLaunch](TCAsyncResult<void> &&)
									{
										if (!mp_pCanDestroyTracker.f_IsEmpty() && !mp_bStopped)
											fp_LaunchApp(*pAppLaunch, false);
									}
								;
							}
							AppLaunch.m_Launch.f_Clear();
							AppLaunch.m_LaunchSubscription.f_Clear();
						}
						break;
					case EProcessLaunchState_LaunchFailed:
						{
							AppLaunch.m_Launch.f_Clear();
							AppLaunch.m_LaunchSubscription.f_Clear();
							if (_bInitialLaunch)
								fp_UpdateAppLaunch(fg_ExceptionPointer(DMibErrorInstance(fg_Format("Mongod launch failed: {}", _Change.f_Get<EProcessLaunchState_LaunchFailed>()))));
						}
						break;
					}
				}
			)
		;
		
		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_LogName = _AppLaunch.m_LogCategory;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		auto &Params = Launch.m_Params;

		Params.m_bAllowExecutableLocate = true;
		Params.m_RunAsUser = mp_NodeUser.m_Name;
		Params.m_RunAsGroup = mp_NodeUser.m_Name;
		{
			auto &Limit = Params.m_Limits[EProcessLimit_OpenedFiles];
			Limit.m_Value = CMeteorManagerActor::fs_GetNodeFileLimits();
			Limit.m_MaxValue = CMeteorManagerActor::fs_GetNodeFileLimits();
		}

		CStr NodeHomePath = fp_GetDataPath("node");
		
		if (PackageOptions.m_bSeparateUser)
			NodeHomePath = fg_Format("{}/node_{}", ProgramDirectory, LaunchKey.m_PackageName);
		
		fs_SetupEnvironment(Params);
		Params.m_bMergeEnvironment = true;
		Params.m_Environment["HOME"] = NodeHomePath;
		Params.m_Environment["TMPDIR"] = NodeHomePath + "/.tmp";
		
		try
		{
			if (auto *pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember("NodeEnvironment"))
			{
				for (auto &EnvVar : pValue->f_Object())
				{
					if (!EnvVar.f_Value().f_IsString())
						continue;
					Params.m_Environment[EnvVar.f_Name()] = EnvVar.f_Value().f_String();
				}
			}
			
			TCMap<CStr, CStr> CalculatedSettings;
			CalculatedSettings["Home"] = NodeHomePath;
			CalculatedSettings["TmpDir"] = NodeHomePath + "/.tmp";
			
			if (mp_bIsStaging)
				CalculatedSettings["Staging"] = "true";

			CalculatedSettings["PackageDirectory"] = PackageDirectory;
			
			CalculatedSettings["ManagerInstanceID"] = mp_InstanceId;
			CalculatedSettings["NodeInstanceID"] = fg_RandomID();
			CalculatedSettings["NodeSequence"] = CStr::fs_ToStr(_AppLaunch.f_GetKey().m_iAppSequence);
			
			CalculatedSettings["PackageName"] = CStr::fs_ToStr(_AppLaunch.f_GetKey().m_PackageName);
			CalculatedSettings["BackendIdentifier"] = _AppLaunch.m_BackendIdentifier;
			CalculatedSettings["LocalIP"] = fp_GetAppIPAddress(_AppLaunch);
			CalculatedSettings["WebSSLPort"] = CStr::fs_ToStr(mp_WebSSLPort);
			CalculatedSettings["WebPort"] = CStr::fs_ToStr(mp_WebPort);
			
			if (PackageOptions.m_Type == CMeteorManagerOptions::EPackageType_Meteor)
			{
				CStr Hostname = fp_GetPackageHostname(_AppLaunch.f_GetKey().m_PackageName);
				CalculatedSettings["Hostname"] = Hostname;
				CalculatedSettings["RootURL"] = fp_GetRootURL(Hostname);
			}
			for (auto &Package : mp_Options.m_Packages)
			{
				if (Package.m_Type != CMeteorManagerOptions::EPackageType_Meteor)
					continue;
				CStr Hostname = fp_GetPackageHostname(Package.f_GetName());
				CalculatedSettings[fg_Format("Hostname_{}", Package.f_GetName())] = Hostname;
				CalculatedSettings[fg_Format("RootURL_{}", Package.f_GetName())] = fp_GetRootURL(Hostname);
				CalculatedSettings[fg_Format("LocalURL_{}", Package.f_GetName())] = fp_GetPackageLocalURL(Package.f_GetName());
			}
			
			CStr MongoSSLDirectory = fp_GetMongoSSLDirectory();
			if (!MongoSSLDirectory.f_IsEmpty())
			{
				CalculatedSettings["IsMultiHost"] = "true";
				CalculatedSettings["DDPSelf"] = mp_DDPSelf;
				
				CStr CaCertificatePath = NodeHomePath + "/certificates/MongoCA.crt";
				CStr ClientCertificatePath = NodeHomePath + "/certificates/admin.pem";
				CStr UserNameEncoded;

				try
				{
					CStr UserName = CSSLContext::fs_GetCertificateDistinguishedName_RFC2253(CFile::fs_ReadFile(ClientCertificatePath));
					NHTTP::CURL::fs_PercentEncode(UserNameEncoded, UserName);
				}
				catch (CException const &_Error)
				{
					DMibError(fg_Format("Failed to extract user name from Mongo certificate file: {}", _Error));
				}
				
				CalculatedSettings["MongoHost"] = mp_MongoHost;
				CalculatedSettings["MongoURL"] = fg_Format
					(
						"mongodb://{}@{}:{}/{}?replicaSet={}&authMechanism=MONGODB-X509"
						, UserNameEncoded
						, mp_MongoHost
						, mp_MongoPort
						, mp_MongoReplicaName
						, mp_MongoDatabase
					)
				;
				
				CalculatedSettings["MongoOplogURL"] = fg_Format
					(
						"mongodb://{}@{}:{}/local?authMechanism=MONGODB-X509"
						, UserNameEncoded
						, mp_MongoHost
						, mp_MongoPort
					)
				;

				CalculatedSettings["MongoSSLCaFile"] = CaCertificatePath;
				CalculatedSettings["MongoSSLClientCertFile"] = ClientCertificatePath;
				CalculatedSettings["MongoSelfID"] = fg_Format("{}_{}", mp_MongoHost, mp_MongoPort).f_ReplaceChar('.', '_');
			}
			else
			{
				CalculatedSettings["MongoHost"] = "localhost";
				CalculatedSettings["MongoURL"] = fg_Format("mongodb://localhost:{}/{}", mp_MongoPort, mp_MongoDatabase);
				CalculatedSettings["MongoOplogURL"] = fg_Format("mongodb://localhost:{}/local", mp_MongoPort);
			}
			
			CJSON MeteorSettings;
			{
				auto &PublicMeteorSettings = MeteorSettings["public"];

				CStr Branch;
				CStr Version;
				CStr Platform;
				CStr Config;
				
				CStr VersionString = fsp_GetVersionString();
				(CStr::CParse("{} {} {} {}") >> Branch >> Version >> Platform >> Config).f_Parse(VersionString);
				PublicMeteorSettings["appVersion"] = fg_Format("#{} | {}", Version, Branch);

				{
					auto &VersionHistory = (PublicMeteorSettings["appVersionHistory"] = EJSONType_Array).f_Array();
					for (auto const &HistoryEntry : mp_VersionHistory)
						VersionHistory.f_Insert(CJSON(HistoryEntry));
				}

				if (mp_bIsStaging)
					PublicMeteorSettings["stagingServer"] = true;
			}
			
			if (mp_pCustomization)
			{
				mp_pCustomization->f_CalculateSettings
					(
						CalculatedSettings
						, MeteorSettings
						, _AppLaunch.f_GetKey().m_PackageName
						, mp_AppState
						, mp_Options
						, PackageOptions
						, [&](CStr const &_Name, CEJSON const &_Default) -> CEJSON
						{
							return fp_GetConfigValue(_Name, _Default);
						}
					)
				;
			}
			
			fp_PopulateNodeEnvironment(Params.m_Environment, CalculatedSettings, _AppLaunch, PackageOptions);

			Params.m_Environment["METEOR_SETTINGS"] = MeteorSettings.f_ToString();
		}
		catch (NException::CException const &_Exception)
		{
			if (_bInitialLaunch)
				fp_UpdateAppLaunch(fg_ExceptionPointer(DMibErrorInstance(fg_Format("Failed to setup node launch environment: {}", _Exception))));
			return;
		}
	
		_AppLaunch.m_Launch = fg_ConstructActor<CProcessLaunchActor>();
		
		_AppLaunch.m_Launch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this))
			> [this, pAppLaunch, _bInitialLaunch](TCAsyncResult<CActorSubscription> &&_Subscription)
			{
				auto &AppLaunch = *pAppLaunch;
				
				if (!_Subscription)
				{
					if (_bInitialLaunch)
						fp_UpdateAppLaunch(_Subscription.f_GetException());
					AppLaunch.m_Launch.f_Clear();
					return;
				}
				AppLaunch.m_LaunchSubscription = fg_Move(*_Subscription);
			}
		;
	}
}
