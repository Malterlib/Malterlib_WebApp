// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_WebApp_App_WebAppManager_Server.h"

namespace NMib::NWebApp::NWebAppManager
{
	TCFuture<void> CWebAppManagerActor::fp_SetupPrerequisites_FastCGI()
	{
		if (!mp_bNeedFCGI)
			co_return {};

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr FastCGIDirectory = fp_GetDataPath("FastCGIHome");

		struct CInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
		};

		auto BlockingActorCheckout = fg_BlockingActor();
		auto NodeInfo = co_await
			(
				g_Dispatch(BlockingActorCheckout) /
				[
					ProgramDirectory
					, FastCGIDirectory
					, ThisActor = fg_ThisActor(this)
					, FastCGIUser = mp_FastCGIUser
					, MongoSSLDirectory = fp_GetMongoSSLDirectory()
					, bRunningElevated = mp_bRunningElevated
				]
				() mutable -> TCFuture<CInfo>
				{
					DLog(Info, "Setting up fast cgi user");

					CInfo Info;
					Info.m_User = FastCGIUser;

					try
					{
	#ifdef DPlatformFamily_Windows
						fsp_SetupPrerequisites_ServerUser(Info.m_User, bRunningElevated, Info.m_UserPassword, FastCGIDirectory, MongoSSLDirectory);
	#else
						fsp_SetupPrerequisites_ServerUser(Info.m_User, bRunningElevated, FastCGIDirectory, MongoSSLDirectory);
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

		mp_FastCGIUser = NodeInfo.m_User;
#ifdef DPlatformFamily_Windows
		if (!NodeInfo.m_UserPassword.f_IsEmpty())
			co_await fp_SaveUserPassword(NodeInfo.m_User.m_UserName, NodeInfo.m_UserPassword);
#endif
		co_return {};
	}
}
