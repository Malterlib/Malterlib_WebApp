// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#ifdef DPlatformFamily_Windows
#include <Mib/Core/PlatformSpecific/WindowsFilePath>
#endif

namespace NMib::NWebApp::NWebAppManager
{
	void CWebAppManagerActor::fs_SetupEnvironment(CProcessLaunchParams &_Params)
	{
		_Params.m_bMergeEnvironment = true;
		_Params.m_Environment["LANGUAGE"] = "en_US.UTF-8";
		_Params.m_Environment["LANG"] = "en_US.UTF-8";
		_Params.m_Environment["LC_ALL"] = "en_US.UTF-8";
#ifdef DPlatformFamily_Windows
		_Params.m_Environment["PATH"] = CFile::fs_GetProgramDirectory() / "bin;" + fg_GetSys()->f_GetEnvironmentVariable("PATH");
#else
		_Params.m_Environment["PATH"] = CFile::fs_GetProgramDirectory() / "bin:" + fg_GetSys()->f_GetEnvironmentVariable("PATH");
#endif
	}

	CStr CWebAppManagerActor::fp_ConcatOutput(CStr const &_StdOut, CStr const &_StdErr) const
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

	TCFuture<CStr> CWebAppManagerActor::f_ExtractTar(CStr const &_TarFile, CStr const &_DestinationDir)
	{
		return f_LaunchTool
			(
				CFile::fs_GetProgramDirectory() / "bin/bsdtar"
				, _DestinationDir
				, fg_CreateVector<CStr>
				(
					"--no-same-owner"
					, "--no-xattr"
					, "-xf"
					, _TarFile
				)
				, CStr{"ExtractArchive"}
				, ELogVerbosity_Errors
				, {}
				, true
			)
		;
	}

	TCFuture<CStr> CWebAppManagerActor::f_LaunchTool
		(
			CStr _Executable
			, CStr _WorkingDir
			, TCVector<CStr> _Params
			, CStr _LogCategory
			, ELogVerbosity _LogVerbosity
			, TCMap<CStr, CStr> _Environment
			, bool _bSeparateStdErr
			, CStr _Home
			, CStr _User
			, CStr _Group
#ifdef DPlatformFamily_Windows
			, CStrSecure _UserPassword
#endif
		)
	{
		if (this->f_IsDestroyed() || mp_bStopped)
			co_return "";

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
		LaunchParams.m_bShowLaunched = false;

		if (!_User.f_IsEmpty())
		{
			LaunchParams.m_RunAsUser = _User;
#ifdef DPlatformFamily_Windows
			LaunchParams.m_RunAsUserPassword = _UserPassword;
#else
#endif
			LaunchParams.m_RunAsGroup = _Group;
		}

		if (!_Home.f_IsEmpty())
		{
			LaunchParams.m_Environment["HOME"] = _Home;
			LaunchParams.m_Environment["TMPDIR"] = _Home + "/.tmp";
#ifdef DPlatformFamily_macOS
			LaunchParams.m_Environment["MalterlibOverrideHome"] = "true";
#endif
#ifdef DPlatformFamily_Windows
			LaunchParams.m_Environment["TMP"] = _Home + "/.tmp";
			LaunchParams.m_Environment["TEMP"] = _Home + "/.tmp";
#endif
		}

		TCSharedPointer<bool> pDestroyed = pToolLaunch->m_pDestroyed;
		auto pCleanup = g_OnScopeExitActor / [this, pDestroyed, pToolLaunch]
			{
				if (!*pDestroyed)
					mp_ToolLaunches.f_Remove(*pToolLaunch);
			}
		;

		auto Result = co_await pToolLaunch->m_ProcessLaunch(&CProcessLaunchActor::f_LaunchSimple, fg_Move(Launch));

		if (Result.m_ExitCode != 0)
		{
			CStr ErrorOut;
			if (_bSeparateStdErr)
				ErrorOut = Result.f_GetCombinedOut().f_TrimRight();
			else
				ErrorOut = Result.f_GetStdOut().f_TrimRight();

			co_return DErrorInstance(fg_Format("Tool '{}' exited with: {}\n{}", _LogCategory, Result.m_ExitCode, ErrorOut));
		}

		co_return Result.f_GetStdOut();
	}

	TCFuture<CStr> CWebAppManagerActor::fp_RunToolForVersionCheck(CStr const &_Tool, TCVector<CStr> const &_Arguments)
	{
		return f_LaunchTool(_Tool, CFile::fs_GetProgramDirectory(), _Arguments, "VersionCheck", ELogVerbosity_None);
	}
}
