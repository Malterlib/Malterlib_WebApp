// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_Websocket()
	{
		if (!mp_bNeedWebsocket)
			co_return {};

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr WebsocketDirectory = fp_GetDataPath("WebsocketHome");

		struct CInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
		};

		CInfo NodeInfo;
		{
			auto BlockingActorCheckout = fg_BlockingActor();
			NodeInfo = co_await
				(
					g_Dispatch(BlockingActorCheckout)
					/ [ProgramDirectory, WebsocketDirectory, ThisActor = fg_ThisActor(this), WebsocketUser = mp_WebsocketUser, MongoSSLDirectory = fp_GetMongoSSLDirectory()]
					() mutable -> TCFuture<CInfo>
					{
						DLog(Info, "Setting up websocket user");

						CInfo Info;
						Info.m_User = WebsocketUser;

						try
						{
		#ifdef DPlatformFamily_Windows
							fsp_SetupPrerequisites_ServerUser(Info.m_User, Info.m_UserPassword, WebsocketDirectory, MongoSSLDirectory);
		#else
							fsp_SetupPrerequisites_ServerUser(Info.m_User, WebsocketDirectory, MongoSSLDirectory);
		#endif
							co_return fg_Move(Info);
						}
						catch (NException::CException const &)
						{
							co_return fg_CurrentException();
						}
					}
				)
			;
		}

		mp_WebsocketUser = NodeInfo.m_User;
#ifdef DPlatformFamily_Windows
		if (!NodeInfo.m_UserPassword.f_IsEmpty())
			co_await fp_SaveUserPassword(NodeInfo.m_User.m_UserName, NodeInfo.m_UserPassword);
#endif
		co_return {};
	}
}
