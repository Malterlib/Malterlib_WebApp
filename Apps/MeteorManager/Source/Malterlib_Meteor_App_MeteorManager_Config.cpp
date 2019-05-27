// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Meteor_App_MeteorManager_Server.h"

namespace NMib::NMeteor::NMeteorManager
{
	void CMeteorManagerActor::fp_ParseConfig_DDPSelf()
	{
		CStr HostName = NProcess::NPlatform::fg_Process_GetHostName();
		CStr HostNameHash = NCryptography::CHash_SHA1::fs_DigestFromData(HostName.f_GetStr(), HostName.f_GetLen()).f_GetString().f_Extract(0, 8);

		mp_DDPSelf = "host-" + HostNameHash + "." + mp_Domain;

		if (mp_WebSSLPort != 443)
			mp_DDPSelf = fg_Format("{}:{}", mp_DDPSelf, mp_WebSSLPort);

		DLog(Info, "DDP self: {}", mp_DDPSelf);
	}
	
	void CMeteorManagerActor::fp_ParseConfig()
	{
#ifdef DMibDebug
		mp_Tags["Debug"];
#endif
		if (auto const *pValue = mp_AppState.m_StateDatabase.m_Data.f_GetMember("Tags"))
		{
			for (auto &Tag : pValue->f_Object())
				mp_Tags[Tag.f_Name()];
		}
		
		if (mp_Tags.f_FindEqual("Staging"))
			mp_bIsStaging = true;

		mp_bAllowRobots = fp_GetConfigValue("AllowRobots", true).f_Boolean();
		mp_bStartNgnix = fp_GetConfigValue("StartNgnix", mp_Options.m_bStartNginx).f_Boolean();

		mp_Domain = fp_GetConfigValue("Domain", mp_Options.m_DefaultDomain).f_String();
		if (mp_bIsStaging)
		{
			aint iFirstDomain = mp_Domain.f_FindChar('.');
			if (iFirstDomain < 0)
				DMibError("Failed to manipulate domain for staging");
			
			mp_Domain = mp_Domain.f_Insert(iFirstDomain, "staging");
			
			DMibLog(Info, "Running in STAGING mode");
		}
		
		mp_WebPort = fp_GetConfigValue("WebPort", mp_Options.m_DefaultWebPort).f_Integer();
		mp_WebSSLPort = fp_GetConfigValue("WebSSLPort", mp_Options.m_DefaultWebSSLPort).f_Integer();
		
		mp_MongoDirectory = fp_GetConfigValue("MongoDirectory", mp_Options.m_Mongo.m_Directory).f_String();
		mp_MongoHost = fp_GetConfigValue("MongoHost", mp_Options.m_Mongo.m_Host).f_String();
		mp_MongoPort = fp_GetConfigValue("MongoPort", mp_Options.m_Mongo.m_Port).f_Integer();
		mp_MongoToolsUser = fp_GetConfigValue("MongoToolsUser", mp_Options.m_Mongo.m_ToolsUser).f_String();
		mp_MongoToolsGroup = fp_GetConfigValue("MongoToolsGroup", mp_Options.m_Mongo.m_ToolsGroup).f_String();
		mp_MongoSSLDirectory = fp_GetConfigValue("MongoSSLDirectory", mp_Options.m_Mongo.m_SSLDirectory).f_String();
		mp_MongoDatabase = fp_GetConfigValue("MongoDatabase", mp_Options.m_Mongo.m_DefaultDatabase).f_String();
		mp_MongoReplicaName = fp_GetConfigValue("MongoReplicaName", mp_Options.m_Mongo.m_DefaultReplicaName).f_String();
		mp_LoopbackPrefix = fp_GetConfigValue("LoopbackPrefix", mp_Options.m_LoopbackPrefix).f_Integer();

		fp_ParseConfig_DDPSelf();
	}

	CEJSON CMeteorManagerActor::fp_GetConfigValue(CStr const &_Name, CEJSON const &_Default) const
	{
		if (_Default.f_IsNull())
		{
			auto pValue = mp_AppState.m_ConfigDatabase.m_Data.f_GetMember(_Name);
			if (pValue)
				return *pValue;
			return nullptr;
		}
		else if (_Default.f_IsValid())
			return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, _Default);
		DNeverGetHere;
		return mp_AppState.m_ConfigDatabase.m_Data.f_GetMemberValue(_Name, "");
	}
}
