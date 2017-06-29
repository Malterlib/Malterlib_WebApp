// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	void CMeteorManagerActor::fs_SetupEnvironment(CProcessLaunchParams &_Params)
	{
		_Params.m_bMergeEnvironment = true;
		_Params.m_Environment["LANGUAGE"] = "en_US.UTF-8";
		_Params.m_Environment["LANG"] = "en_US.UTF-8";
		_Params.m_Environment["LC_ALL"] = "en_US.UTF-8";
	}

	CStr CMeteorManagerActor::fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const
	{
		if (_StdOut.f_IsEmpty() && _StdErr.f_IsEmpty())
			return CStr();
		CStr Ret;
		CStr StdOut = _StdOut.f_Trim();
		if (!StdOut.f_IsEmpty())
			fg_AddStrSep(Ret, StdOut, DMibNewLine);
		CStr StdErr = _StdErr.f_Trim();
		if (!StdErr.f_IsEmpty())
			fg_AddStrSep(Ret, StdErr, DMibNewLine);
		return DMibNewLine + Ret;
	}

	TCContinuation<CStr> CMeteorManagerActor::f_ExtractTar(CStr const &_TarFile, CStr const &_DestinationDir)
	{
		return f_LaunchTool
			(
				"tar"
				, _DestinationDir
				, fg_CreateVector<CStr>("--no-same-owner", "-xf", _TarFile)
				, CStr{"ExtractArchive"}
				, ELogVerbosity_Errors
				, {}
				, true
				, {}
				, {}
			)
		;
	}

	TCContinuation<CStr> CMeteorManagerActor::f_LaunchTool
		(
			CStr const &_Executable
			, CStr const &_WorkingDir
			, TCVector<CStr> const &_Params
			, CStr const &_LogCategory
			, ELogVerbosity _LogVerbosity
			, TCMap<CStr, CStr> const &_Environment
			, bool _bSeparateStdErr
			, CStr const &_Home
			, CStr const &_User
		)
	{
		if (mp_pCanDestroyTracker.f_IsEmpty() || mp_bStopped)
			return fg_Explicit("");
		
		auto *pToolLaunch = &mp_ToolLaunches.f_Insert();
		pToolLaunch->m_ProcessLaunch = fg_ConstructActor<CProcessLaunchActor>();
		
		CProcessLaunchActor::CSimpleLaunch Launch = NMib::NProcess::CProcessLaunchParams::fs_LaunchExecutable(_Executable, _Params, _WorkingDir, {});
		
		switch (_LogVerbosity)
		{
		case ELogVerbosity_None:
			break;
		case ELogVerbosity_Errors:
			Launch.m_ToLog = CProcessLaunchActor::ELogFlag_Error;
			break;
		case ELogVerbosity_Messages:
			Launch.m_ToLog = CProcessLaunchActor::ELogFlag_Error | CProcessLaunchActor::ELogFlag_Info;
			break;
		case ELogVerbosity_All:
			Launch.m_ToLog = CProcessLaunchActor::ELogFlag_All;
			break;
		}
		Launch.m_LogName = _LogCategory;
		Launch.m_Params.m_bCreateNewProcessGroup = true;
		
		auto &LaunchParams = Launch.m_Params;
		
		fs_SetupEnvironment(LaunchParams);
		
		LaunchParams.m_Environment += _Environment;

		LaunchParams.m_bSeparateStdErr = _bSeparateStdErr;
		LaunchParams.m_bAllowExecutableLocate = true;
		
		if (!_User.f_IsEmpty())
		{
			LaunchParams.m_RunAsUser = _User;
			LaunchParams.m_RunAsGroup = _User;
		}

		if (!_Home.f_IsEmpty())
		{
			LaunchParams.m_Environment["HOME"] = _Home;
			LaunchParams.m_Environment["TMPDIR"] = _Home + "/.tmp";
		}
		
		TCSharedPointer<bool> pDestroyed = pToolLaunch->m_pDestroyed;
		auto pCleanup = g_OnScopeExitActor > [this, pDestroyed, pToolLaunch]
			{
				if (!*pDestroyed)
					mp_ToolLaunches.f_Remove(*pToolLaunch);
			}
		;
		
		TCContinuation<CStr> Continuation;
		pToolLaunch->m_ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch))
			> Continuation / [this, pCleanup, Continuation, _bSeparateStdErr, _LogCategory](CProcessLaunchActor::CSimpleLaunchResult &&_Result)
			{
				if (_Result.m_ExitCode != 0)
				{
					CStr ErrorOut;
					if (_bSeparateStdErr)
						ErrorOut = _Result.f_GetErrorOut().f_TrimRight();
					else
						ErrorOut = _Result.f_GetStdOut().f_TrimRight();
					Continuation.f_SetException(DErrorInstance(fg_Format("Tool '{}' exited with: {}\n{}", _LogCategory, _Result.m_ExitCode, ErrorOut)));
					return;
				}
				Continuation.f_SetResult(_Result.f_GetStdOut());
			}
		;
		
		return Continuation;
	}

	TCContinuation<CStr> CMeteorManagerActor::fp_RunToolForVersionCheck(CStr const &_Tool, TCVector<CStr> const &_Arguments)
	{
		return f_LaunchTool(_Tool, CFile::fs_GetProgramDirectory(), _Arguments, "VersionCheck", ELogVerbosity_None);
	}
}
