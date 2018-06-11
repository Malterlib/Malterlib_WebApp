// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	TCContinuation<void> CMeteorManagerActor::fp_SetupPrerequisites_Websocket()
	{
		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr WebsocketDirectory = fp_GetDataPath("WebsocketHome");
		
		struct CInfo
		{
			CUser m_User = {"", ""};
		};
	
		TCContinuation<void> Continuation;
		g_Dispatch(*mp_FileActors)
			> [ProgramDirectory, WebsocketDirectory, ThisActor = fg_ThisActor(this), WebsocketUser = mp_WebsocketUser, MongoSSLDirectory = fp_GetMongoSSLDirectory()]
			() mutable -> TCContinuation<CInfo>
			{
				DLog(Info, "Setting up fast cgi user");
				
				TCContinuation<CInfo> Continuation;
				
				try
				{
					fsp_SetupPrerequisites_ServerUser(WebsocketUser, WebsocketDirectory, MongoSSLDirectory);
					
					CInfo Info;
					Info.m_User = WebsocketUser;

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
				Continuation.f_SetResult();
			}
		;

		return Continuation;
	}
}
