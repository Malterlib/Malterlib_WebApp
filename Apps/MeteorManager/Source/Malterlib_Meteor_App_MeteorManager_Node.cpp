// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/Concurrency/Actor/Timer>

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

	TCContinuation<void> CMeteorManagerActor::fp_StartApps()
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
			}
		}
		
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
	}
	
	void CMeteorManagerActor::fp_SetupNodeArguments(TCVector<CStr> &o_Arguments, CAppLaunch const &_AppLaunch, CMeteorManagerOptions::CPackage const &_PackageOptions)
	{
		o_Arguments.f_Insert(fg_Format("--max_old_space_size={}", (_PackageOptions.m_MemoryPerNode*1024.0).f_ToInt()));
		
		if (fp_GetConfigValue("NodeDebug", false).f_Boolean())
			o_Arguments.f_Insert(fg_Format("--debug={}", 7000 + mp_Options.m_LoopbackPrefix*1000 + _AppLaunch.f_GetKey().m_iAppSequence));

		o_Arguments.f_Insert(_PackageOptions.m_CustomParams);
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
		CStr WorkingDirectory = fg_Format("{}/{}", ProgramDirectory, _AppLaunch.f_GetKey().m_PackageName);

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
				, WorkingDirectory
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
