// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Concurrency/LogError>

namespace NMib::NWebApp::NWebAppManager
{
	TCFuture<uint32> CWebAppManagerActor::f_LaunchAsApp
		(
			NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine
			, CStr _Application
			, CStr _Executable
			, TCVector<CStr> _Params
			, CStr _WorkingDir
		)
	{
		CAppLaunch const *pAppLaunch = nullptr;
		TCSet<CStr> AllLaunches;

		for (auto &AppLaunchEntry : mp_AppLaunches.f_Entries())
		{
			if (AppLaunchEntry.f_Key().m_PackageName == _Application)
			{
				pAppLaunch = &AppLaunchEntry.f_Value();
				break;
			}

			AllLaunches[AppLaunchEntry.f_Key().m_PackageName];
		}

		if (!pAppLaunch)
		{
			*_pCommandLine %= "Could not find any app launch with name '{}'. Existing app launches: {}/n"_f << _Application << AllLaunches;
			co_return 1;
		}

		if (!pAppLaunch->m_LaunchEnvironment)
		{
			*_pCommandLine %= "The application '{}' has not launched yet so we cannot launch in the environment/n"_f << _Application;
			co_return 1;
		}

		auto &LaunchEnvironment = *pAppLaunch->m_LaunchEnvironment;

		CProcessLaunchActor::CLaunch Launch = CProcessLaunchParams::fs_LaunchExecutable
			(
				_Executable
				, _Params
				, _WorkingDir
				, {}
			)
		;

		Launch.m_Params.m_Environment = LaunchEnvironment.m_Environment;
		Launch.m_Params.m_RunAsUser = LaunchEnvironment.m_RunAsUser;
		Launch.m_Params.m_RunAsUserPassword = LaunchEnvironment.m_RunAsUserPassword;
		Launch.m_Params.m_RunAsGroup = LaunchEnvironment.m_RunAsGroup;
		Launch.m_Params.m_bAllowExecutableLocate = true;

		TCPromiseFuturePair<void> LaunchedPromise;
		TCPromiseFuturePair<uint32> ExitedPromise;
		Launch.m_Params.m_fOnStateChange = [LaunchedPromise = fg_Move(LaunchedPromise.m_Promise), ExitedPromise = fg_Move(ExitedPromise.m_Promise)]
			(CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
			{
				switch (_State.f_GetTypeID())
				{
				case EProcessLaunchState_Launched:
					{
						LaunchedPromise.f_SetResult();
					}
					break;
				case EProcessLaunchState_LaunchFailed:
					{
						auto &LaunchError = _State.f_Get<EProcessLaunchState_LaunchFailed>();
						LaunchedPromise.f_SetException(DMibErrorInstance(LaunchError));
					}
					break;
				case EProcessLaunchState_Exited:
					{
						auto ExitStatus = _State.f_Get<EProcessLaunchState_Exited>();
						ExitedPromise.f_SetResult(ExitStatus);
					}
					break;
				}
			}
		;

		Launch.m_Params.m_fOnOutput = [_pCommandLine](EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
			{
				if (_Output.f_IsEmpty())
					return;

				if (_OutputType != EProcessLaunchOutputType_StdOut)
					*_pCommandLine += _Output;
				else
					*_pCommandLine %= _Output;
			}
		;

		TCActor<CProcessLaunchActor> ProcessLaunchActor(fg_Construct());
		auto DestroyLaunch = co_await fg_AsyncDestroy(ProcessLaunchActor);

		auto CancellationSubscription = co_await _pCommandLine->f_RegisterForCancellation
			(
				g_ActorFunctor / [ProcessLaunchActor] -> TCFuture<bool>
				{
					co_await ProcessLaunchActor(&CProcessLaunchActor::f_StopProcess);
					co_return true;
				}
			)
		;

		auto LaunchSubscription = co_await ProcessLaunchActor(&CProcessLaunchActor::f_Launch, fg_Move(Launch), fg_ThisActor(this));

		co_await fg_Move(LaunchedPromise.m_Future);

		auto StdInSubscription = co_await _pCommandLine->f_RegisterForStdInBinary
			(
				g_ActorFunctor / [ProcessLaunchActor, _pCommandLine, bEOFReceived = false](EStdInReaderOutputType _Type, CIOByteVector _Input, CStr _Error) mutable -> TCFuture<void>
				{
					switch (_Type)
					{
					case EStdInReaderOutputType_StdIn:
						{
							bool bShouldStop = _Input.f_Contains(3) >= 0;

							co_await ProcessLaunchActor(&CProcessLaunchActor::f_SendStdInBinary, fg_Move(_Input));

							if (bShouldStop)
							{
								co_await fg_Timeout(1.0);
								co_await ProcessLaunchActor(&CProcessLaunchActor::f_StopProcess);
								break;
							}
						}
						break;
					case EStdInReaderOutputType_GeneralError:
						{
							*_pCommandLine %= _Error;
						}
						break;
					case EStdInReaderOutputType_EndOfFile:
						{
						if (!bEOFReceived)
							{
								bEOFReceived = true;
								co_await ProcessLaunchActor(&CProcessLaunchActor::f_CloseStdIn).f_Wrap() > fg_LogError("", "Failed to close stdin");
							}
						}
						break;
					}

					co_return {};
				}
				, EStdInReaderFlag_None
			)
		;

		co_await fg_Move(ExitedPromise.m_Future);

		co_return {};
	}
}
