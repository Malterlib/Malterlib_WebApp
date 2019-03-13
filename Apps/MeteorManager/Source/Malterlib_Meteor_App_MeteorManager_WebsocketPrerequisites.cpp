// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	TCFuture<void> CMeteorManagerActor::fp_SetupPrerequisites_Websocket()
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
	
		TCPromise<void> Promise;
		g_Dispatch(*mp_FileActors)
			> [ProgramDirectory, WebsocketDirectory, ThisActor = fg_ThisActor(this), WebsocketUser = mp_WebsocketUser, MongoSSLDirectory = fp_GetMongoSSLDirectory()]
			() mutable -> TCFuture<CInfo>
			{
				DLog(Info, "Setting up websocket user");
				
				TCPromise<CInfo> Promise;
				CInfo Info;
				Info.m_User = WebsocketUser;
				
				try
				{
#ifdef DPlatformFamily_Windows
					fsp_SetupPrerequisites_ServerUser(Info.m_User, Info.m_UserPassword, WebsocketDirectory, MongoSSLDirectory);
#else
					fsp_SetupPrerequisites_ServerUser(Info.m_User, WebsocketDirectory, MongoSSLDirectory);
#endif
					Promise.f_SetResult(Info);
					return Promise.f_MoveFuture();
				}
				catch (NException::CException const &)
				{
					Promise.f_SetCurrentException();
					return Promise.f_MoveFuture();
				}
			}
			> Promise / [this, Promise](CInfo const &_NodeInfo)
			{
				mp_WebsocketUser = _NodeInfo.m_User;
#ifdef DPlatformFamily_Windows
				if (!_NodeInfo.m_UserPassword.f_IsEmpty())
				{
					mp_AppState.m_StateDatabase.m_Data["Users"][_NodeInfo.m_User.m_UserName]["Password"] = _NodeInfo.m_UserPassword;
					mp_AppState.f_SaveStateDatabase() > Promise;
					return;
				}
#endif
				Promise.f_SetResult();
			}
		;

		return Promise.f_MoveFuture();
	}
}
