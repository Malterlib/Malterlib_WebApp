// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

#include <Mib/Container/Registry>

namespace NMib::NWebApp::NWebAppManager
{
	CStr CWebAppManagerActor::fsp_GetVersionString()
	{
		NMib::NProcess::CVersionInfo VersionInfo;
		NMib::NProcess::NPlatform::fg_Process_GetVersionInfo(CFile::fs_GetProgramPath(), VersionInfo);

		return fg_Format
			(
			 	"{} {}.{}.{} {} {} {} {}"
				, VersionInfo.m_Branch
				, VersionInfo.m_Major
				, VersionInfo.m_Minor
				, VersionInfo.m_Revision
				, DMibStringize(DPlatform)
				, DMibStringize(DConfig)
				, VersionInfo.m_GitBranch
				, VersionInfo.m_GitCommit
			)
		;
	}

	TCFuture<void> CWebAppManagerActor::fp_UpdateVersionHistory()
	{
		TCPromise<void> Promise;

		g_Dispatch(*mp_FileActors) / [VersionHistoryFileName = fp_GetDataPath("VersionHistory.txt")]
			{
				CStr VersionString = fsp_GetVersionString();

				CRegistry HistoryRegistry;
				if (CFile::fs_FileExists(VersionHistoryFileName))
					HistoryRegistry.f_ParseStr(CFile::fs_ReadStringFromFile(VersionHistoryFileName, true), VersionHistoryFileName);

				if (!HistoryRegistry.f_GetChildNoPath(VersionString))
					HistoryRegistry.f_SetValueNoPath(VersionString, fg_Format("{}", CTime::fs_NowUTC().f_GetSeconds()));

				struct CVersion
				{
					CTime m_Time;
					CStr m_Version;

					auto operator <=> (CVersion const &_Right) const = default;
				};

				TCVector<CVersion> Versions;

				for (auto iChild = HistoryRegistry.f_GetChildIterator(); iChild; ++iChild)
				{
					CVersion Version;
					Version.m_Version = iChild->f_GetName();
					Version.m_Time = CTime::fs_Create(iChild->f_GetThisValue().f_ToInt(uint64(0)));
					Versions.f_Insert(Version);
				}

				Versions.f_Sort();

				CRegistry NewHistoryRegistry;

				mint nMaxVersions = 10;
				mint nVersions = 0;

				TCVector<CStr> VersionHistory;

				for (auto iVersion = Versions.f_GetIterator(); iVersion && nVersions < nMaxVersions; ++iVersion, ++nVersions)
				{
					NewHistoryRegistry.f_SetValueNoPath(iVersion->m_Version, CStr::fs_ToStr( iVersion->m_Time.f_GetSeconds()));

					CStr Branch;
					CStr Version;
					CStr Platform;
					CStr Config;
					CStr GitBranch = "UNKNOWN";
					CStr GitCommit = "UNKNOWN";

					(CStr::CParse("{} {} {} {} {} {}") >> Branch >> Version >> Platform >> Config >> GitBranch >> GitCommit).f_Parse(iVersion->m_Version);

					CTime LocalTime = iVersion->m_Time.f_ToLocal();

					CTimeConvert::CDateTime DateTime;
					CTimeConvert(LocalTime).f_ExtractDateTime(DateTime);

					VersionHistory.f_Insert
						(
							"{sf0,sj2}:{sf0,sj2} / #{} | {} | {td} | {} | {}"_f << DateTime.m_Hour << DateTime.m_Minute << Version << Branch << LocalTime << GitBranch << GitCommit
						)
					;
				}

				CFile::fs_WriteStringToFile(VersionHistoryFileName, NewHistoryRegistry.f_GenerateStr(), false);

				return VersionHistory;
			}
			> Promise / [this, Promise](TCVector<CStr> &&_VersionHistory)
			{
				mp_VersionHistory = fg_Move(_VersionHistory);
				Promise.f_SetResult();
			}
		;
		return Promise.f_MoveFuture();
	}
}
