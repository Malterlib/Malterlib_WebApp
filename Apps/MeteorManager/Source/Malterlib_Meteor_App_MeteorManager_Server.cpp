// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

#include <Mib/File/ExeFS>
#include <Mib/File/VirtualFS>
#include <Mib/File/VirtualFSs/MalterlibFS>
#include <Mib/Encoding/JSONShortcuts>

namespace NMib::NMeteor::NMeteorManager
{
	CMeteorManagerActor::CMeteorManagerActor(CDistributedAppState &_AppState, CMeteorManagerOptions const &_Options)
		: mp_AppState(_AppState)
		, mp_NodeUser{fg_Format("mib_node_{}", _Options.m_ManagerName)}
		, mp_pCanDestroyTracker(fg_Construct())
		, mp_Options(_Options)
	{
#ifdef DPlatformFamily_OSX
		CStr Path = fg_GetSys()->f_GetEnvironmentVariable("PATH");
		if (Path.f_Find("/opt/local/bin") < 0)
			fg_GetSys()->f_SetEnvironmentVariable("PATH", "/opt/local/bin:" + Path);
#endif
	}
	
	CMeteorManagerActor::~CMeteorManagerActor()
	{
	}

	TCContinuation<void> CMeteorManagerActor::f_Startup()
	{
		mp_pFileActor = fg_ConstructActor<CSeparateThreadActor>(fg_Construct("File actor"));

		DLog(Info, "Extracting ExeFS");
		
		TCContinuation<void> Continuation;

		fp_CleanupOldProcesses() > Continuation % "Failed to clean up old processes" / [this, Continuation]
			{
				fp_ExtractExeFS() > Continuation % "Failed to extract ExeFS" / [this, Continuation]
					{
						DLog(Info, "Done extracting ExeFS");
						fp_CheckVersion(fp_GetNodeExecutable("node"), "--version", "v{}.{}.{}", mp_Version_Node)
							+ fp_SetupPrerequisites_Node()
							> Continuation / [this, Continuation]
							{
								fp_StartApps() > Continuation;
							}
						;
					}
				;
			}
		;
		
		return Continuation;
	}
	
	TCContinuation<void> CMeteorManagerActor::f_PreStop()
	{
		DLog(Debug, "Pre-stop server");
		mp_bStopped = true;
		
		TCActorResultVector<void> Destroys;
		for (auto &ToolLaunch : mp_ToolLaunches)
			ToolLaunch.m_ProcessLaunch->f_Destroy() > Destroys.f_AddResult();
		
		TCContinuation<void> Continuation;
		
		Destroys.f_GetResults()
			> [this, Continuation](auto &&)
			{
				fp_DestroyApps() > [this, Continuation](auto &&)
					{
						DLog(Debug, "Pre-stop server done");
						Continuation.f_SetResult();
					}
				;
			}
		;
		
		return Continuation;
	}

	TCContinuation<void> CMeteorManagerActor::fp_Destroy()
	{
		DLog(Debug, "Destroy server");
		auto pCanDestroy = fg_Move(mp_pCanDestroyTracker);
		
		TCActorResultVector<void> Destroys;
		for (auto &ToolLaunch : mp_ToolLaunches)
			ToolLaunch.m_ProcessLaunch->f_Destroy() > Destroys.f_AddResult();
		
		Destroys.f_GetResults()
			> [this, pCanDestroy](auto &&_Results)
			{
				TCActorResultVector<void> Destroys;
				
				Destroys.f_GetResults() > [this, pCanDestroy](auto &&_Results)
					{
						fp_DestroyApps() > [this, pCanDestroy](auto &&)
							{
								DLog(Debug, "Destroy apps done");
							}
						;
					}
				;
			}
		;
		
		return pCanDestroy->m_Continuation;
	}
	
	void CMeteorManagerActor::fsp_SetupUser(CUser &_User)
	{
		if (!NSys::fg_UserManagement_GroupExists(_User.m_Name, _User.m_GroupID))
			NSys::fg_UserManagement_CreateGroup(_User.m_Name, _User.m_GroupID);

		if (!NSys::fg_UserManagement_UserExists(_User.m_Name, _User.m_UserID))
		{
			NSys::fg_UserManagement_CreateUser
				(
					_User.m_Name
					, _User.m_Name
					, ""
					, _User.m_Name
					, "/dev/null"
					, _User.m_UserID
				)
			;
		}
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_ExtractExeFS() const
	{
		return fg_Dispatch
			(
				mp_pFileActor
				, []
				{
					CExeFS ExeFS;
					if (!fg_OpenExeFS(ExeFS))
						DError("Failed to open ExeFS");
					
					CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
					
					CFileSystemInterface_VirtualFS MalterlibFS(ExeFS.m_FileSystem);
					CFileSystemInterface_Disk DiskFS;
					
					MalterlibFS.f_CopyFilesWithAttribs("*", DiskFS, ProgramDirectory);
				}
			)
		;
	}
	
	TCContinuation<void> CMeteorManagerActor::fp_CheckVersion(CStr const &_Tool, CStr const &_Argument, CStr const &_ParseString, CVersion const &_NeededVersion)
	{
		TCContinuation<void> Continuation;
		fp_RunToolForVersionCheck(_Tool, fg_CreateVector<CStr>(_Argument)) > Continuation % "Failed to check version" / [=](CStr &&_Data)
			{
				if (_Data.f_IsEmpty())
				{
					Continuation.f_SetException(DErrorInstance(fg_Format("Failed get version with: {} {}", _Tool, _Argument)));
					return;
				}
				
				CVersion Version;
				aint nParsed = 0;
				(CStr::CParse(_ParseString) >> Version.m_Major >> Version.m_Minor >> Version.m_Revision).f_Parse(_Data, nParsed);
				
				if (nParsed != 3)
				{
					Continuation.f_SetException(DErrorInstance(fg_Format("Failed to extract {} version from: {}", _Tool, _Data)));
					return;
				}
				
				if (Version < _NeededVersion)
				{
					Continuation.f_SetException(DErrorInstance(fg_Format("{} version {} is less than the required version of {}", _Tool, Version, _NeededVersion)));
					return;
				}
				DLog(Info, "{} version {} found", _Tool, Version);
				Continuation.f_SetResult();
			}
		;
		return Continuation;
	}

	CStr CMeteorManagerActor::fp_GetDataPath(CStr const &_Path) const
	{
		return CFile::fs_AppendPath(CFile::fs_GetProgramDirectory(), _Path);
	}
	
	void CMeteorManagerOptions::f_ParseSettings(CStr const &_Settings, CStr const &_FileName)
	{
		CEJSON Settings = CEJSON::fs_FromString(_Settings, _FileName);
		
		for (auto &Package : Settings.f_Object())
		{
			m_PackageNames.f_Insert(Package.f_Name());
		}
	}
}
