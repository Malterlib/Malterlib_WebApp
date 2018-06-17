// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Websocket()
	{
		if (!mp_bNeedWebsocket)
			return fg_Explicit();

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr WebsocketDirectory = fp_GetDataPath("WebsocketHome");
		
		struct CInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
		};
	
		TCContinuation<void> Continuation;
		g_Dispatch(*mp_FileActors)
			> [ProgramDirectory, WebsocketDirectory, ThisActor = fg_ThisActor(this), WebsocketUser = mp_WebsocketUser, MongoSSLDirectory = fp_GetMongoSSLDirectory()]
			() mutable -> TCContinuation<CInfo>
			{
				DLog(Info, "Setting up fast cgi user");
				
				TCContinuation<CInfo> Continuation;
				CInfo Info;
				Info.m_User = WebsocketUser;
				
				try
				{
#ifdef DPlatformFamily_Windows
					fsp_SetupPrerequisites_ServerUser(Info.m_User, Info.m_UserPassword, WebsocketDirectory, MongoSSLDirectory);
#else
					fsp_SetupPrerequisites_ServerUser(Info.m_User, WebsocketDirectory, MongoSSLDirectory);
#endif
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
				mp_WebsocketUser = _NodeInfo.m_User;
#ifdef DPlatformFamily_Windows
				if (!_NodeInfo.m_UserPassword.f_IsEmpty())
				{
					mp_AppState.m_StateDatabase.m_Data["Users"][_NodeInfo.m_User.m_UserName]["Password"] = _NodeInfo.m_UserPassword;
					mp_AppState.f_SaveStateDatabase() > Continuation;
					return;
				}
#endif
				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}
}
