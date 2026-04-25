// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/Concurrency/Actor/Timer>
#include <Mib/Cryptography/Certificate>
#include <Mib/Concurrency/LogError>

namespace NMib::NWebApp::NWebAppManager
{
	umint CWebAppManagerActor::fs_GetNodeFileLimits()
	{
		return 65536 + 8192;
	}

	CStr CWebAppManagerActor::fp_GetNodeExecutable(CStr const &_Executable, bool _bUseSystemNode)
	{
		bool bUseInternalNode = mp_Options.m_bUseInternalNode && !_bUseSystemNode;
#ifdef DPlatformFamily_Windows
		if (bUseInternalNode)
			return "{}/node_dist/{}.exe"_f << CFile::fs_GetProgramDirectory() << _Executable;
		else
			return "{}.exe"_f << _Executable;
#else
		if (bUseInternalNode)
			return "{}/node_dist/bin/{}"_f << CFile::fs_GetProgramDirectory() << _Executable;
		else
			return _Executable;
#endif
	}

	CStr CWebAppManagerActor::fp_GetPackageHostname(CStr const &_PackageName, EHostnamePrefix _Prefix) const
	{
		auto &Package = mp_Options.m_Packages[_PackageName];
		if (!Package.f_IsServer())
			DMibError("Cannot get package hostname for non-server package");

		CStr Prefix = Package.m_DomainPrefix;

		switch (_Prefix)
		{
		case EHostnamePrefix_Static: Prefix += "static"; break;
		case EHostnamePrefix_StaticSource: Prefix += "staticsource"; break;
		case EHostnamePrefix_None: break;
		}

		if (Prefix.f_IsEmpty())
			return mp_Domain;

		return fg_Format("{}.{}", Prefix, mp_Domain);
	}

	CStr CWebAppManagerActor::fp_GetPackageRoot(CStr const &_PackageName) const
	{
		auto &Package = mp_Options.m_Packages[_PackageName];

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		if (Package.m_ExternalRoot.f_IsEmpty())
			return ProgramDirectory / Package.f_GetName();
		else
			return fp_GetConfigValue("ExternalRoot_{}"_f << _PackageName, CFile::fs_GetExpandedPath(ProgramDirectory / Package.m_ExternalRoot)).f_String();
	}

	CStr CWebAppManagerActor::fp_GetPackageSocketRoot(CStr const &_PackageName) const
	{
		auto &Package = mp_Options.m_Packages[_PackageName];
		if (!Package.f_IsServer())
			DMibError("Cannot get package root path for non-server package");

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		return ProgramDirectory / "Sockets" / Package.f_GetName();
	}

	CStr CWebAppManagerActor::fp_GetRootURL(CStr const &_Hostname, CStr const &_SubPath) const
	{
		if (!_SubPath.f_IsEmpty())
		{
			if (mp_WebSSLPort == 443)
				return fg_Format("https://{}/{}/", _Hostname, _SubPath);
			else
				return fg_Format("https://{}:{}/{}/", _Hostname, mp_WebSSLPort, _SubPath);
		}
		else
		{
			if (mp_WebSSLPort == 443)
				return fg_Format("https://{}/", _Hostname);
			else
				return fg_Format("https://{}:{}/", _Hostname, mp_WebSSLPort);
		}
	}

	CStr CWebAppManagerActor::fp_GetAppIPAddress(CAppLaunch const &_AppLaunch, bool _bForMalterlib) const
	{
		auto &LaunchKey = _AppLaunch.f_GetKey();

		if (_AppLaunch.m_bUnixSocket)
		{
			if (_bForMalterlib)
			{
				// The folder where the socket lives already have the correct permissions so the socket can have everyone access.
				return fg_Format("UNIX(666):{}/socket-{}", fp_GetPackageSocketRoot(LaunchKey.m_PackageName), LaunchKey.m_iAppSequence);
			}
			else
				return fg_Format("unix:{}/socket-{}", fp_GetPackageSocketRoot(LaunchKey.m_PackageName), LaunchKey.m_iAppSequence);
		}
		else
			return fg_Format("127.{}.{}.1", mp_LoopbackPrefix, 2 + LaunchKey.m_iAppSequence);
	}

	CStr CWebAppManagerActor::fp_GetAppLocalURL(CAppLaunch const &_AppLaunch, umint _iPort) const
	{
		return fg_Format("http://{}{}{}/", fp_GetAppIPAddress(_AppLaunch, false), (_AppLaunch.m_bUnixSocket ? "." : ":"), mp_LocalPort + _iPort);
	}

	CStr CWebAppManagerActor::fp_GetPackageLocalURL(CStr const &_PackageName) const
	{
		auto &CurrentURL = mp_CurrentPackageLocalURL[_PackageName];
		umint iURL = CurrentURL;

		auto &Package = mp_Options.m_Packages[_PackageName];
		++CurrentURL;
		if (CurrentURL >= Package.m_Concurrency)
			CurrentURL = 0;

		return mp_PackageLocalURLs[_PackageName][iURL];
	}

	void CWebAppManagerActor::fp_CreateAppLaunches()
	{
		for (auto &Package : mp_Options.m_Packages)
		{
			if ((Package.m_Type == CWebAppManagerOptions::EPackageType_Custom && Package.m_CustomExecutable.f_IsEmpty()) || Package.f_IsStatic())
				continue;

			if (Package.m_Type == CWebAppManagerOptions::EPackageType_Websocket)
				mp_bNeedWebsocket = true;
			else if (Package.m_Type == CWebAppManagerOptions::EPackageType_FastCGI)
				mp_bNeedFCGI = true;
			else
				mp_bNeedNode = true;

			CAppLaunchKey LaunchKey;
			LaunchKey.m_PackageName = Package.f_GetName();
			for (umint i = 0; i < Package.m_Concurrency; ++i)
			{
				LaunchKey.m_iAppSequence = mp_AppSequence++;
				auto &AppLaunch = mp_AppLaunches[LaunchKey];
				++mp_OutstandingLaunches[Package.f_GetName()];
				if (Package.m_Concurrency > 1)
					AppLaunch.m_LogCategory = fg_Format("{}-{}", LaunchKey.m_PackageName, i);
				else
					AppLaunch.m_LogCategory = LaunchKey.m_PackageName;

				AppLaunch.m_BackendIdentifier = fg_Format("{}_{}", LaunchKey.m_PackageName, fg_RandomID());
				AppLaunch.m_bMalterlibDistributedApp = Package.m_bMalterlibDistributedApp;
				AppLaunch.m_bUnixSocket = Package.m_bUnixSocket;

				for (umint i = 0; i < Package.m_PortConcurrency; ++i)
					mp_PackageLocalURLs[LaunchKey.m_PackageName].f_Insert(fp_GetAppLocalURL(AppLaunch, i));
			}
		}
	}

	TCFuture<void> CWebAppManagerActor::fp_SetupNetworkTunnels()
	{
		if (!mp_bNeedNode)
			co_return {};

		mp_NetworkTunnelsServer = fg_Construct(mp_AppState.m_DistributionManager, mp_AppState.m_TrustManager, mp_AppState.f_AuditorFactory(), "Network Tunnel", "NetworkTunnel");
		mp_NetworkTunnelsServer(&CNetworkTunnelsServer::f_Start) > fg_LogError("Tunnel Server", "Start tunnels server");

		co_return {};
	}

	TCFuture<void> CWebAppManagerActor::fp_StartApps()
	{
		fp_UpdateAppLaunch(nullptr);

		co_return co_await mp_AppLaunchesPromise.f_Future();
	}

	TCFuture<void> CWebAppManagerActor::fp_DestroyApps()
	{
		TCFutureVector<void> Destroys;

		for (auto &Launch : mp_AppLaunches)
		{
			if (Launch.m_Launch.f_IsOfType<void>())
				continue;
			else if (Launch.m_Launch.f_IsOfType<CNormalProcessLaunch>())
				fg_Move(Launch.m_Launch.f_GetAsType<CNormalProcessLaunch>().m_Launch).f_Destroy() > Destroys;
			else
			{
				auto &pLaunchInfo = Launch.m_Launch.f_GetAsType<TCUniquePointer<CDistributedApp_LaunchInfo>>();
				if (pLaunchInfo && pLaunchInfo->m_Launch)
					fg_Move(pLaunchInfo->m_Launch).f_Destroy() > Destroys;
				else
					Launch.m_DestroyPromise.f_Set<1>().f_Future() > Destroys;
			}
		}

		co_await fg_AllDone(Destroys).f_Wrap() > fg_LogError("AppLaunches", "Destroying apps failed");

		co_return {};
	}

	void CWebAppManagerActor::fp_UpdateAppLaunch(CExceptionPointer const &_pException)
	{
		if (_pException)
		{
			if (!mp_AppLaunchesPromise.f_IsSet())
				mp_AppLaunchesPromise.f_SetException(_pException);
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
					if (!mp_AppLaunchesPromise.f_IsSet())
					{
						mp_AppLaunchesPromise.f_SetException
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

		if (bAllDone && !mp_AppLaunchesPromise.f_IsSet())
			mp_AppLaunchesPromise.f_SetResult();
	}

	void CWebAppManagerActor::fp_SetupNodeArguments(TCVector<CStr> &o_Arguments, CAppLaunch const &_AppLaunch, CWebAppManagerOptions::CPackage const &_PackageOptions)
	{
		o_Arguments.f_Insert(fg_Format("--max_old_space_size={}", (_PackageOptions.m_MemoryPerNode*1024.0).f_ToInt()));

		o_Arguments.f_Insert("--trace-deprecation");
		o_Arguments.f_Insert("--trace-warnings");
		o_Arguments.f_Insert("--enable-source-maps");

		if (!_AppLaunch.m_bUnixSocket)
		{
			if (fp_GetConfigValue("NodeDebug", false).f_Boolean())
				o_Arguments.f_Insert("--inspect={}:5599"_f << fp_GetAppIPAddress(_AppLaunch, false));
			else
				o_Arguments.f_Insert("--inspect-port={}:5599"_f << fp_GetAppIPAddress(_AppLaunch, false));
		}

		o_Arguments.f_Insert(_PackageOptions.m_CustomParams);
	}

	void CWebAppManagerActor::fp_PopulateNodeEnvironment
		(
			CSystemEnvironment &o_Environment
			, TCMap<CStr, CStr> const &_CalculatedSettings
			, CAppLaunch const &_AppLaunch
			, CWebAppManagerOptions::CPackage const &_PackageOptions
		)
	{
		TCSet<CStr> AvailableTags = mp_Tags;
		AvailableTags[_PackageOptions.f_GetName()];
		AvailableTags[fg_Format("{}_{}", _PackageOptions.f_GetName(), _AppLaunch.f_GetKey().m_iAppSequence)];

		switch(_PackageOptions.m_Type)
		{
		case CWebAppManagerOptions::EPackageType_Meteor: AvailableTags["Meteor"]; break;
		case CWebAppManagerOptions::EPackageType_FastCGI: AvailableTags["FastCGI"]; break;
		case CWebAppManagerOptions::EPackageType_Websocket: AvailableTags["Websocket"]; break;
		case CWebAppManagerOptions::EPackageType_Npm: AvailableTags["Npm"]; break;
		case CWebAppManagerOptions::EPackageType_Custom: AvailableTags["Custom"]; break;
		case CWebAppManagerOptions::EPackageType_Static: AvailableTags["Static"]; break;
		}

		for (auto &EnvVar : mp_Options.m_Environment)
		{
			bool bPassAllTags = true;

			for (auto &Tags : EnvVar.m_RequiredTags)
			{
				bool bFoundOne = false;
				for (auto &Tag : Tags)
				{
					if (AvailableTags.f_FindEqual(Tag))
						bFoundOne = true;
				}
				if (!bFoundOne)
					bPassAllTags = false;
			}

			for (auto &Tag : EnvVar.m_ForbiddenTags)
			{
				if (AvailableTags.f_FindEqual(Tag))
					bPassAllTags = false;
			}

			if (!bPassAllTags)
				continue;

			if (auto *pSetting = _CalculatedSettings.f_FindEqual(EnvVar.m_Setting))
			{
				o_Environment[EnvVar.f_GetName()] = *pSetting;
				continue;
			}

			CEJsonSorted Value;
			if (EnvVar.m_Default)
				Value = fp_GetConfigValue(EnvVar.m_Setting, *EnvVar.m_Default);
			else
				Value = fp_GetConfigValue(EnvVar.m_Setting, nullptr);

			if (Value.f_IsNull())
				continue;

			o_Environment[EnvVar.f_GetName()] = Value.f_AsString();
		}
	}

	void CWebAppManagerActor::fp_HandleNodeDebuggerOutput(CAppLaunch &_AppLaunch, CStr const &_StdErr)
	{
		if (_AppLaunch.m_bUnixSocket)
			return;

		for (auto &Line : _StdErr.f_SplitLine<true>())
		{
			if (!Line.f_StartsWith("Debugger listening on "))
				continue;
			CStr Protocol;
			CStr Host;
			CStr GUID;
			aint nParsed = 0;
			(CStr::CParse("Debugger listening on {}://{}/{}") >> Protocol >> Host >> GUID).f_Parse(Line, nParsed);

			if (nParsed != 3)
				continue;

			CEJsonSorted Metadata;
			Metadata["URLTemplate"] = "{Host}:{Port}";

			TCFuture<void> OldSubscriptionDestroy;
			if (_AppLaunch.m_TunnelSubscription)
				OldSubscriptionDestroy = _AppLaunch.m_TunnelSubscription->f_Destroy();
			else
				OldSubscriptionDestroy = g_Void;

			fg_Move(OldSubscriptionDestroy) > [this, pAppLaunch = &_AppLaunch, Metadata = fg_Move(Metadata)](TCAsyncResult<void> &&) mutable
				{
					mp_NetworkTunnelsServer
						(
							&CNetworkTunnelsServer::f_PublishNetworkTunnel
							, pAppLaunch->m_LogCategory
							, fp_GetAppIPAddress(*pAppLaunch, false)
							, 5599
							, fg_Move(Metadata)
						)
						> fg_LogError("Network Tunnel", "Publish network tunnel") / [pAppLaunch](CActorSubscription &&_Subscription)
						{
							pAppLaunch->m_TunnelSubscription = fg_Move(_Subscription);
						}
					;
				}
			;
		}
	}

	void CWebAppManagerActor::fp_LaunchApp(CAppLaunch &_AppLaunch, bool _bInitialLaunch)
	{
		if (!_AppLaunch.m_Launch.f_IsOfType<void>() || mp_bStopped || f_IsDestroyed())
			return; // Launch already in progress

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();

		TCVector<CStr> Arguments;
		auto *pAppLaunch = &_AppLaunch;
		auto &LaunchKey = _AppLaunch.f_GetKey();

		CStr LaunchExecutable;
		CStr PackageDirectory = fg_Format("{}/{}", ProgramDirectory, _AppLaunch.f_GetKey().m_PackageName);

		auto &PackageOptions = fg_Const(mp_Options.m_Packages)[LaunchKey.m_PackageName];

		bool bIsNode = false;

		if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_Meteor)
		{
			LaunchExecutable = fp_GetNodeExecutable("node", PackageOptions.m_bUseSystemNode);
			fp_SetupNodeArguments(Arguments, _AppLaunch, PackageOptions);
			bIsNode = true;

			Arguments.f_Insert(fg_Format("{}/{}/main.js", ProgramDirectory, LaunchKey.m_PackageName));
		}
		else if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_Npm)
		{
			LaunchExecutable = fp_GetNodeExecutable("node", PackageOptions.m_bUseSystemNode);
			fp_SetupNodeArguments(Arguments, _AppLaunch, PackageOptions);
			bIsNode = true;

			Arguments.f_Insert(ProgramDirectory / LaunchKey.m_PackageName / PackageOptions.m_PackageSubDir / PackageOptions.m_MainFile);
		}
		else if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_Custom)
		{
			if (PackageOptions.m_CustomExecutable.f_IsEmpty())
			{
				if (_bInitialLaunch)
					fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Missing executable for custom launch: {}", LaunchKey.m_PackageName))));
				return;
			}
			LaunchExecutable = ProgramDirectory / LaunchKey.m_PackageName / PackageOptions.m_CustomExecutable;
			Arguments = PackageOptions.m_CustomParams;
		}
		else if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_FastCGI)
		{
			if (PackageOptions.m_CustomExecutable.f_IsEmpty())
			{
				if (_bInitialLaunch)
					fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Missing executable for FastCGI launch: {}", LaunchKey.m_PackageName))));
				return;
			}
			LaunchExecutable = ProgramDirectory / LaunchKey.m_PackageName / PackageOptions.m_CustomExecutable;
			Arguments = PackageOptions.m_CustomParams;
		}
		else if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_Websocket)
		{
			if (PackageOptions.m_CustomExecutable.f_IsEmpty())
			{
				if (_bInitialLaunch)
					fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Missing executable for Websocket launch: {}", LaunchKey.m_PackageName))));
				return;
			}
			LaunchExecutable = ProgramDirectory / LaunchKey.m_PackageName / PackageOptions.m_CustomExecutable;
			Arguments = PackageOptions.m_CustomParams;
		}
		else
		{
			if (_bInitialLaunch)
				fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance("Invalid package type")));
			return;
		}

		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				LaunchExecutable
				, Arguments
				, PackageDirectory
				, [this, pAppLaunch, _bInitialLaunch](CProcessLaunchStateChangeVariant const &_Change, fp64 _TimeSinceStart)
				{
					auto &AppLaunch = *pAppLaunch;

					DLogCategoryStr(AppLaunch.m_LogCategory);

					switch (_Change.f_GetTypeID())
					{
					case EProcessLaunchState_Launched:
						{
							if (f_IsDestroyed() || mp_bStopped)
							{
								if (AppLaunch.m_Launch.f_IsOfType<CNormalProcessLaunch>())
								{
									auto &NormalLaunch = AppLaunch.m_Launch.f_Get<1>();
									if (NormalLaunch.m_Launch)
										NormalLaunch.m_Launch(&CProcessLaunchActor::f_StopProcess).f_DiscardResult();
								}
								if (_bInitialLaunch)
									fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance("Application is being destroyed")));
							}
							else
							{
								if (_bInitialLaunch && AppLaunch.m_Launch.f_IsOfType<CNormalProcessLaunch>())
								{
									--mp_OutstandingLaunches[AppLaunch.f_GetKey().m_PackageName];
									fp_UpdateAppLaunch(nullptr);
								}
							}
						}
						break;
					case EProcessLaunchState_Exited:
						{
							if (AppLaunch.m_DestroyPromise)
								AppLaunch.m_DestroyPromise->f_SetResult();

							if (!f_IsDestroyed() && !mp_bStopped)
							{
								if (_bInitialLaunch)
									fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Unexpected exit {}", _Change.f_Get<EProcessLaunchState_Exited>()))));

								DLog(Info, "Unexpected exit {}, scheduling relaunch in 10 seconds", _Change.f_Get<EProcessLaunchState_Exited>());
								fg_Timeout(10.0) > [this, pAppLaunch]() -> TCFuture<void>
									{
										if (!f_IsDestroyed() && !mp_bStopped)
											fp_LaunchApp(*pAppLaunch, false);

										co_return {};
									}
								;
							}
							AppLaunch.m_Launch.f_Set<0>();
						}
						break;
					case EProcessLaunchState_LaunchFailed:
						{
							if (AppLaunch.m_DestroyPromise)
								AppLaunch.m_DestroyPromise->f_SetResult();
							AppLaunch.m_Launch.f_Set<0>();
							if (_bInitialLaunch)
								fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Launch failed: {}", _Change.f_Get<EProcessLaunchState_LaunchFailed>()))));
						}
						break;
					}
				}
			)
		;

		if (bIsNode)
		{
			Launch.m_Params.m_fOnOutput = [this, pAppLaunch](EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
				{
					if (_OutputType == EProcessLaunchOutputType_StdOut)
					{
						for (auto &Line : _Output.f_SplitLine<true>())
						{
							if (Line == "MalterlibInvalidateCloudFrontCaches")
								fp_InvalidateCloudfrontDistributions() > fg_LogError("Cloud Front", "Failed to invalidate CloudFront distributions in response to application request");
						}
					}
					if (_OutputType != EProcessLaunchOutputType_StdErr)
						return;
					fp_HandleNodeDebuggerOutput(*pAppLaunch, _Output);
				}
			;
		}

		Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
		Launch.m_LogName = _AppLaunch.m_LogCategory;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		Launch.m_bWholeLineOutput = true;

		auto &Params = Launch.m_Params;

		Params.m_bAllowExecutableLocate = true;
		Params.m_bShowLaunched = false;

		CStr LaunchHomePath;
		CUser LaunchUser{"", ""};

		if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_FastCGI)
		{
			LaunchHomePath = fp_GetDataPath("FastCGIHome");
			LaunchUser = mp_FastCGIUser;
		}
		else if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_Websocket)
		{
			LaunchHomePath = fp_GetDataPath("WebsocketHome");
			LaunchUser = mp_WebsocketUser;
		}
		else
		{
			LaunchHomePath = fp_GetDataPath("node");
			LaunchUser = mp_NodeUser;
		}

		if (PackageOptions.m_bSeparateUser)
		{
			LaunchHomePath = fg_Format("{}/Home_{}", ProgramDirectory, LaunchKey.m_PackageName);
			LaunchUser = PackageOptions.m_User;
		}

		Params.m_RunAsUser = LaunchUser.m_UserName;
#ifdef DPlatformFamily_Windows
		Params.m_RunAsUserPassword = fp_GetUserPassword(LaunchUser.m_UserName);
#endif
		Params.m_RunAsGroup = LaunchUser.m_GroupName;

		{
			auto &Limit = Params.m_Limits[EProcessLimit_OpenedFiles];
			auto MaxFiles = NProcess::NPlatform::fg_Process_GetMaxFilesPerProc();
			if (MaxFiles)
			{
				Limit.m_Value = fg_Min(MaxFiles, CWebAppManagerActor::fs_GetNodeFileLimits());
				Limit.m_MaxValue = Limit.m_Value;
			}
		}

		fs_SetupEnvironment(Params);
		Params.m_bMergeEnvironment = true;
		Params.m_Environment["HOME"] = LaunchHomePath;
		Params.m_Environment["TMPDIR"] = LaunchHomePath + "/.tmp";
#ifdef DPlatformFamily_macOS
		Params.m_Environment["MalterlibOverrideHome"] = "true";
#endif
#ifdef DPlatformFamily_Windows
		Params.m_Environment["TMP"] = LaunchHomePath + "/.tmp";
		Params.m_Environment["TEMP"] = LaunchHomePath + "/.tmp";
#endif

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
			CalculatedSettings["Home"] = LaunchHomePath;
			CalculatedSettings["TmpDir"] = LaunchHomePath + "/.tmp";

			if (mp_bIsStaging)
				CalculatedSettings["Staging"] = "true";

			CalculatedSettings["PackageDirectory"] = PackageDirectory;

			CalculatedSettings["ManagerInstanceID"] = mp_InstanceId;
			CalculatedSettings["LaunchInstanceID"] = fg_RandomID();
			CalculatedSettings["LaunchSequence"] = CStr::fs_ToStr(_AppLaunch.f_GetKey().m_iAppSequence);

			CalculatedSettings["PackageName"] = CStr::fs_ToStr(_AppLaunch.f_GetKey().m_PackageName);
			CalculatedSettings["BackendIdentifier"] = _AppLaunch.m_BackendIdentifier;
			CalculatedSettings["LocalIP"] = fp_GetAppIPAddress(_AppLaunch, false);
			CalculatedSettings["LocalIPMalterlib"] = fp_GetAppIPAddress(_AppLaunch, true);
			CalculatedSettings["NginxGroup"] = mp_NginxUser.m_GroupName;
			CalculatedSettings["LocalPort"] = CStr::fs_ToStr(mp_LocalPort);
			CalculatedSettings["PortConcurrency"] = CStr::fs_ToStr(PackageOptions.m_PortConcurrency);
			CalculatedSettings["WebSSLPort"] = CStr::fs_ToStr(mp_WebSSLPort);
			CalculatedSettings["WebPort"] = CStr::fs_ToStr(mp_WebPort);

			if (PackageOptions.f_IsServer())
			{
				CStr Hostname = fp_GetPackageHostname(_AppLaunch.f_GetKey().m_PackageName, EHostnamePrefix_None);
				CStr HostnameStatic = fp_GetPackageHostname(_AppLaunch.f_GetKey().m_PackageName, EHostnamePrefix_Static);
				CalculatedSettings["Hostname"] = Hostname;
				CalculatedSettings["RootURL"] = fp_GetRootURL(Hostname, PackageOptions.m_SubPath);

				if (fp_GetConfigValue("EnableSeparateStaticRoot", false).f_Boolean())
				{
					CalculatedSettings["StaticHostname"] = HostnameStatic;
					CalculatedSettings["StaticRootURL"] = fp_GetRootURL(HostnameStatic, PackageOptions.m_SubPath);
				}
			}
			for (auto &Package : mp_Options.m_Packages)
			{
				if (!Package.f_IsDynamicServer())
					continue;
				CStr Hostname = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_None);
				CStr HostnameStatic = fp_GetPackageHostname(Package.f_GetName(), EHostnamePrefix_Static);
				CalculatedSettings[fg_Format("Hostname_{}", Package.f_GetName())] = Hostname;
				CalculatedSettings[fg_Format("RootURL_{}", Package.f_GetName())] = fp_GetRootURL(Hostname, Package.m_SubPath);
				CalculatedSettings[fg_Format("LocalURL_{}", Package.f_GetName())] = fp_GetPackageLocalURL(Package.f_GetName());
			}

#ifdef DMibWebAppManager_SupportMongo
			CalculatedSettings["MongoURL"] = fp_GetDBAddress(mp_MongoDatabase, LaunchHomePath / "certificates");
			CalculatedSettings["MongoOplogURL"] = fp_GetDBAddress("local", LaunchHomePath / "certificates");

			if (mp_bConnectToExternalMongo)
			{
				CalculatedSettings["MongoSSLCaFile"] = LaunchHomePath / "certificates/MongoCA.crt";
				CalculatedSettings["MongoSSLClientCertFile"] = LaunchHomePath / "certificates/admin.crt";
				CalculatedSettings["MongoSSLClientKeyFile"] = LaunchHomePath / "certificates/admin.key";
			}
#endif

			CJsonSorted MeteorSettings;
			{
				auto &PublicMeteorSettings = MeteorSettings["public"];

				CStr Branch;
				CStr Version;
				CStr Platform;
				CStr Config;
				CStr GitBranch;
				CStr GitCommit;

				CStr VersionString = fsp_GetVersionString();
				(CStr::CParse("{} {} {} {} {} {}") >> Branch >> Version >> Platform >> Config >> GitBranch >> GitCommit).f_Parse(VersionString);
				PublicMeteorSettings["appVersion"] = fg_Format("#{} | {}", Version, Branch);
				PublicMeteorSettings["gitBranch"] = GitBranch;
				PublicMeteorSettings["gitCommit"] = GitCommit;

				{
					auto &VersionHistory = (PublicMeteorSettings["appVersionHistory"] = EJsonType_Array).f_Array();
					for (auto const &HistoryEntry : mp_VersionHistory)
						VersionHistory.f_Insert(CJsonSorted(HistoryEntry));
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
						, PackageOptions
						, fp_GetImpl()
					)
				;
			}

			fp_PopulateNodeEnvironment(Params.m_Environment, CalculatedSettings, _AppLaunch, PackageOptions);

			if (PackageOptions.m_Type == CWebAppManagerOptions::EPackageType_Meteor)
				Params.m_Environment["METEOR_SETTINGS"] = MeteorSettings.f_ToString();
			else
				Params.m_Environment["APPLICATION_SETTINGS"] = MeteorSettings.f_ToString();
		}
		catch (NException::CException const &_Exception)
		{
			if (_bInitialLaunch)
				fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Failed to setup node launch environment: {}", _Exception))));
			return;
		}

		_AppLaunch.m_LaunchEnvironment = CLaunchEnvironment
			{
				.m_Environment = Params.m_Environment
				, .m_RunAsUser = Params.m_RunAsUser
				, .m_RunAsUserPassword = Params.m_RunAsUserPassword
				, .m_RunAsGroup = Params.m_RunAsGroup
			}
		;

		if (_AppLaunch.m_bMalterlibDistributedApp)
		{
			if (!mp_AppLaunchHelper)
			{
				CDistributedApp_LaunchHelperDependencies Dependencies;
				Dependencies.m_TrustManager = this->mp_AppState.m_TrustManager;
				Dependencies.m_DistributionManager = this->mp_AppState.m_DistributionManager;
				Dependencies.m_Address = this->mp_AppState.m_LocalAddress;
				mp_AppLaunchHelper = fg_Construct(Dependencies, false);
			}

			_AppLaunch.m_Launch.f_Set<2>();

			mp_AppLaunchHelper(&CDistributedApp_LaunchHelper::f_LaunchWithLaunch, _AppLaunch.m_LogCategory, fg_Move(Launch), fg_ThisActor(this))
				> [this, pAppLaunch, _bInitialLaunch](TCAsyncResult<CDistributedApp_LaunchInfo> &&_LaunchInfo) mutable
				{
					auto &AppLaunch = *pAppLaunch;

					if (!_LaunchInfo)
					{
						if (_bInitialLaunch)
							fp_UpdateAppLaunch(fg_MakeException(DMibErrorInstance(fg_Format("Launch failed: {}", _LaunchInfo.f_GetExceptionStr()))));

						if (AppLaunch.m_DestroyPromise)
							AppLaunch.m_DestroyPromise.f_Get().f_SetResult();
						AppLaunch.m_Launch.f_Set<0>();
						return;
					}

					auto &pLaunchInfo = AppLaunch.m_Launch.f_Get<2>();
					pLaunchInfo = fg_Construct(fg_Move(*_LaunchInfo));

					if (AppLaunch.m_DestroyPromise)
					{
						if (pLaunchInfo->m_Subscription)
						{
							pLaunchInfo->f_Destroy() > AppLaunch.m_DestroyPromise->f_ReceiveAny();
							AppLaunch.m_DestroyPromise.f_Clear();
						}
						else if (pLaunchInfo->m_Launch)
							pLaunchInfo->m_Launch(&CProcessLaunchActor::f_StopProcess).f_DiscardResult();

						return;
					}

					if (_bInitialLaunch)
					{
						--mp_OutstandingLaunches[pAppLaunch->f_GetKey().m_PackageName];
						fp_UpdateAppLaunch(nullptr);
					}
				}
			;

			return;
		}

		auto &NormalLaunch = _AppLaunch.m_Launch.f_Set<1>();

		NormalLaunch.m_Launch = fg_ConstructActor<CProcessLaunchActor>();
		NormalLaunch.m_Launch(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this))
			> [this, pAppLaunch, _bInitialLaunch](TCAsyncResult<CActorSubscription> &&_Subscription)
			{
				auto &AppLaunch = *pAppLaunch;

				if (!_Subscription)
				{
					if (_bInitialLaunch)
						fp_UpdateAppLaunch(_Subscription.f_GetException());
					AppLaunch.m_Launch.f_Set<0>();
					return;
				}
				auto &NormalLaunch = AppLaunch.m_Launch.f_Get<1>();
				NormalLaunch.m_Subscription = fg_Move(*_Subscription);
			}
		;
	}
}
