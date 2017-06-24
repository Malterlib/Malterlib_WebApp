// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	CEJSON CMeteorManagerActor::fp_GetConfigValue(CStr const &_Name, CEJSON const &_Default) const
	{
		if (_Name == "MongoDirectory")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_Directory);
		if (_Name == "MongoHost")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_Host);
		if (_Name == "MongoPort")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_Port);
		if (_Name == "MongoToolsUser")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_ToolsUser);
		if (_Name == "MongoToolsGroup")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_ToolsGroup);
		if (_Name == "MongoSSLDirectory")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_SSLDirectory);
		if (_Name == "MongoDefaultDatabase")
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, mp_Options.m_Mongo.m_DefaultDatabase);
		
		return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, _Default);
	}
}
