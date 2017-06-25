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
		, mp_pCustomization(fg_CreateMeteorManagerCustomization())
		, mp_FileActors(4)
	{
	}
	
	CMeteorManagerActor::~CMeteorManagerActor()
	{
	}

	TCContinuation<void> CMeteorManagerActor::f_Startup()
	{
		DMibLogWithCategory
			(
				MeteorManager
				, Info
				, "Meteor Manager ({}) starting, {} {}.{}{} {} {}"
				, mp_Options.m_ManagerName
				, DMalterlibBranch
				, DMibStringize(DProductVersionMajor)
				, DMibStringize(DProductVersionMinor)
				, DMibStringize(DProductVersionRevision)
				, DMibStringize(DPlatform)
				, DMibStringize(DConfig)
			)
		;
		
		mp_FileActors.f_Construct(fg_Construct(fg_Construct<CSeparateThreadActor>(), "File actor"));

		
		TCContinuation<void> Continuation;

		DLog(Info, "Cleaning up old processes");
		fp_CleanupOldProcesses() > Continuation % "Failed to clean up old processes" / [this, Continuation]
			{
				DLog(Info, "Done cleaning up, extracting ExeFS");
				fp_ExtractExeFS() > Continuation % "Failed to extract ExeFS" / [this, Continuation]
					{
						DLog(Info, "Done extracting ExeFS, setting up node prerequisites and updating version history");
						fp_SetupPrerequisites_Node()
							+ fp_UpdateVersionHistory()
							> Continuation / [this, Continuation]
							{
								DLog(Info, "Done setting up node prerequisites and updating version history, checking node version");
								fp_CheckVersion(fp_GetNodeExecutable("node"), "--version", "v{}.{}.{}", mp_Version_Node)
									> Continuation / [this, Continuation]
									{
										DLog(Info, "Done checking node version, setting up mongo");
										fp_SetupMongo() > Continuation / [this, Continuation]
											{
												DLog(Info, "Done setting up mongo, starting apps");
												fp_StartApps() > Continuation;
											}
										;
									}
								;
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
								mp_FileActors.f_Destroy() > pCanDestroy->f_Track();
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
		return g_Dispatch(*mp_FileActors) > []
			{
				CExeFS ExeFS;
				if (!fg_OpenExeFS(ExeFS))
					DError("Failed to open ExeFS");
				
				CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
				
				CFileSystemInterface_VirtualFS MalterlibFS(ExeFS.m_FileSystem);
				CFileSystemInterface_Disk DiskFS;
				CTime SourceFileTime = MalterlibFS.f_GetWriteTime("");
				
				CTime DestinationFileTime;
				CStr ExeFSFileTimeFile = ProgramDirectory + "/ExeFS.time";
				
				if (CFile::fs_FileExists(ExeFSFileTimeFile))
				{
					TCBinaryStreamFile<> Stream;
					Stream.f_Open(ExeFSFileTimeFile, EFileOpen_Read | EFileOpen_ShareAll);
					Stream >> DestinationFileTime;
				}
				if (SourceFileTime == DestinationFileTime)
					return;
				
				MalterlibFS.f_CopyFilesWithAttribs("*", DiskFS, ProgramDirectory);

				TCBinaryStreamFile<> Stream;
				Stream.f_Open(ExeFSFileTimeFile, EFileOpen_Write | EFileOpen_ShareAll);
				Stream << SourceFileTime;
			}
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
	
	CMeteorManagerOptions::CMeteorManagerOptions(CStr const &_ManagerName)
		: m_ManagerName(_ManagerName)
	{
	}

	void CMeteorManagerOptions::f_ParseSettings(CStr const &_Settings, CStr const &_FileName)
	{
		CEJSON Settings = CEJSON::fs_FromString(_Settings, _FileName);
		
		for (auto &PackageJSON : Settings["Packages"].f_Object())
		{
			auto &Package = m_Packages[PackageJSON.f_Name()];
			auto &PackageSettings = PackageJSON.f_Value().f_Object();
			
			auto &PackageType = PackageJSON.f_Value()["Type"].f_String();
			if (PackageType == "Meteor")
				Package.m_Type = EPackageType_Meteor;
			else if (PackageType == "Npm")
				Package.m_Type = EPackageType_Npm;
			else if (PackageType == "Custom")
				Package.m_Type = EPackageType_Custom;
			else
				DMibError(fg_Format("Unknown package type: {}", PackageType));
			
			if (auto *pValue = PackageSettings.f_GetMember("MemoryPerNode"))
				Package.m_MemoryPerNode = pValue->f_AsFloat(1.5);
			
			if (auto *pValue = PackageSettings.f_GetMember("Concurrency", EJSONType_Integer))
				Package.m_Concurrency = pValue->f_Integer();
			else if (auto *pValue = PackageSettings.f_GetMember("Concurrency", EJSONType_String))
			{
				CStr Expression = pValue->f_String();
				Expression = Expression.f_Replace("{PhysicalMemoryGB}", fg_Format("{}", fp64(NProcess::NPlatform::fg_Process_GetPhysicalMemory()) / (1024.0*1024.0*1024.0)));
				Expression = Expression.f_Replace("{MemoryPerNode}", fg_Format("{}", Package.m_MemoryPerNode));

				Expression = fg_Format("{{xpr({})}", Expression);
				
				CStr EvaluatedExpression = fg_Format(Expression.f_GetStr(), 0.0);
				
				Package.m_Concurrency = fg_Min(fg_Max(EvaluatedExpression.f_ToFloat().f_ToInt(), 1), NSys::fg_Thread_GetVirtualCores());
			}

			if (auto *pValue = PackageSettings.f_GetMember("StartupDependencies"))
			{
				for (auto &Dependency : pValue->f_Array())
					Package.m_StartupDependencies.f_Insert(Dependency.f_String());
			}
			
			if (auto *pValue = PackageSettings.f_GetMember("CustomExecutable"))
				Package.m_CustomExecutable = pValue->f_String();

			if (auto *pValue = PackageSettings.f_GetMember("CustomParams"))
			{
				for (auto &Dependency : pValue->f_Array())
					Package.m_CustomParams.f_Insert(Dependency.f_String());
			}
			
			if (auto *pValue = PackageSettings.f_GetMember("SeparateUser"))
				Package.m_bSeparateUser = pValue->f_Boolean();
		}
		
		if (auto *pValue = Settings.f_GetMember("LoopbackPrefix"))
			m_LoopbackPrefix = pValue->f_Integer();

		auto &MongoJSON = Settings["Mongo"].f_Object();
		if (auto *pValue = MongoJSON.f_GetMember("Directory"))
			m_Mongo.m_Directory = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("Host"))
			m_Mongo.m_Host = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("Port"))
			m_Mongo.m_Port = pValue->f_Integer();
		if (auto *pValue = MongoJSON.f_GetMember("ToolsUser"))
			m_Mongo.m_ToolsUser = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("ToolsGroup"))
			m_Mongo.m_ToolsGroup = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("SSLDirectory"))
			m_Mongo.m_SSLDirectory = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("DatabaseSetupScript"))
			m_Mongo.m_DatabaseSetupScript = pValue->f_String();
		if (auto *pValue = MongoJSON.f_GetMember("DefaultDatabase"))
			m_Mongo.m_DefaultDatabase = pValue->f_String();
	}
	
	ICMeteorManagerCustomization::ICMeteorManagerCustomization() = default;
	ICMeteorManagerCustomization::~ICMeteorManagerCustomization() = default;

	void ICMeteorManagerCustomization::f_SetupNodeEnvironment
		(
			CSystemEnvironment &o_Environment
			, CStr const &_PackageName
			, CDistributedAppState const &_AppState
			, CMeteorManagerOptions const &_Options
		)
	{
	}
}
