// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	TCFuture<void> CMeteorManagerActor::fp_SetupPrerequisites_FastCGI()
	{
		TCPromise<void> Promise;

		if (!mp_bNeedFCGI)
			return Promise <<= g_Void;

		CStr ProgramDirectory = CFile::fs_GetProgramDirectory();
		CStr FastCGIDirectory = fp_GetDataPath("FastCGIHome");
		
		struct CInfo
		{
			CUser m_User = {"", ""};
#ifdef DPlatformFamily_Windows
			CStrSecure m_UserPassword;
#endif
		};
	
		g_Dispatch(*mp_FileActors) /
			[
				ProgramDirectory
				, FastCGIDirectory
				, ThisActor = fg_ThisActor(this)
				, FastCGIUser = mp_FastCGIUser
				, MongoSSLDirectory = fp_GetMongoSSLDirectory()
			]
			() mutable -> TCFuture<CInfo>
			{
				TCPromise<CInfo> Promise;

				DLog(Info, "Setting up fast cgi user");
				
				CInfo Info;
				Info.m_User = FastCGIUser;
				
				try
				{
#ifdef DPlatformFamily_Windows
					fsp_SetupPrerequisites_ServerUser(Info.m_User, Info.m_UserPassword, FastCGIDirectory, MongoSSLDirectory);
#else
					fsp_SetupPrerequisites_ServerUser(Info.m_User, FastCGIDirectory, MongoSSLDirectory);
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
				mp_FastCGIUser = _NodeInfo.m_User;
#ifdef DPlatformFamily_Windows
				if (!_NodeInfo.m_UserPassword.f_IsEmpty())
				{
					fp_SaveUserPassword(_NodeInfo.m_User.m_UserName, _NodeInfo.m_UserPassword) > Promise;
					return;
				}
#endif
				Promise.f_SetResult();
			}
		;

		return Promise.f_MoveFuture();
	}
}
