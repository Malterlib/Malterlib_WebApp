// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_FastCGI()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr FastCGIDirectory = fp_GetDataPath("FastCGIHome");
		
		struct CInfo
		{
			CUser m_User = {"", ""};
		};
	
		TCContinuation<void> Continuation;
		g_Dispatch(*mp_FileActors)
			> [ProgramDirectory, FastCGIDirectory, ThisActor = fg_ThisActor(this), FastCGIUser = mp_FastCGIUser, MongoSSLDirectory = fp_GetMongoSSLDirectory()]
			() mutable -> TCContinuation<CInfo>
			{
				DLog(Info, "Setting up fast cgi user");
				
				TCContinuation<CInfo> Continuation;
				
				try
				{
					fsp_SetupPrerequisites_ServerUser(FastCGIUser, FastCGIDirectory, MongoSSLDirectory);
					
					CInfo Info;
					Info.m_User = FastCGIUser;

					Continuation.f_SetResult(Info);
					return Continuation;
				}
				catch (NException::CException const &)
				{
					Continuation.f_SetCurrentException();
					return Continuation;
				}
			}
			> Continuation / [this, Continuation](CInfo const &_NodeInfo)
			{
				mp_FastCGIUser = _NodeInfo.m_User;
				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}
}
