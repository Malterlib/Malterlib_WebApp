// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	CStr CMeteorManagerActor::fsp_GetVersionString()
	{
		return fg_Format
			(
				"{} {}.{}{} {} {}"
				, DMalterlibBranch
				, DMibStringize(DProductVersionMajor)
				, DMibStringize(DProductVersionMinor)
				, DMibStringize(DProductVersionRevision)
				, DMibStringize(DPlatform)
				, DMibStringize(DConfig)
			)
		;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_UpdateVersionHistory()
	{
		TCContinuation<void> Continuation;
		
		g_Dispatch(*mp_FileActors) > [VersionHistoryFileName = fp_GetDataPath("VersionHistory.txt")]
			{
				CStr VersionString = fsp_GetVersionString();

				CRegistry_CStr HistoryRegistry;
				if (CFile::fs_FileExists(VersionHistoryFileName))
					HistoryRegistry.f_ParseStr(CFile::fs_ReadStringFromFile(VersionHistoryFileName, true), VersionHistoryFileName);

				if (!HistoryRegistry.f_GetChildNoPath(VersionString))
					HistoryRegistry.f_SetValueNoPath(VersionString, fg_Format("{}", CTime::fs_NowUTC().f_GetSeconds()));

				struct CVersion
				{
					CTime m_Time;
					CStr m_Version;

					bool operator < (CVersion const &_Right) const
					{
						return fg_TupleReferences(_Right.m_Time, _Right.m_Version) < fg_TupleReferences(this->m_Time, this->m_Version);
					}
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

				CRegistry_CStr NewHistoryRegistry;

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

					(CStr::CParse("{} {} {} {}") >> Branch >> Version >> Platform >> Config).f_Parse(iVersion->m_Version);

					CTime LocalTime = iVersion->m_Time.f_ToLocal();

					CTimeConvert::CDateTime DateTime;
					CTimeConvert(LocalTime).f_ExtractDateTime(DateTime);

					VersionHistory.f_Insert(fg_Format("{sf0,sj2}:{sf0,sj2} / #{} | {} | {td}", DateTime.m_Hour, DateTime.m_Minute, Version, Branch, LocalTime));
				}

				CFile::fs_WriteStringToFile(VersionHistoryFileName, NewHistoryRegistry.f_GenerateStr(), false);
				
				return VersionHistory;
			}
			> Continuation / [this, Continuation](TCVector<CStr> &&_VersionHistory)
			{
				mp_VersionHistory = fg_Move(_VersionHistory);
				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}
}
