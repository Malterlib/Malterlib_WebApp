// Copyright © 2020 Nonna Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Encoding/JSONShortcuts>
#include <Mib/Cryptography/RandomID>
#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<uint32> CAcmeManagerActor::fp_CommandLine_DomainRemove(CEJSONSorted const &_Params, NStorage::TCSharedPointer<CCommandLineControl> const &_pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		if (!mp_Domains.f_FindEqual(Name))
			co_return Auditor.f_Exception(fg_Format("No such domain '{}'", Name));

		mp_Domains.f_Remove(Name);

		if (auto *pDomainState = mp_State.m_StateDatabase.m_Data.f_GetMember("Domains"))
		{
			if (pDomainState->f_GetMember(Name))
				pDomainState->f_RemoveMember(Name);
		}

		co_await (mp_State.m_StateDatabase.f_Save() % "[Remove domain] Failed to save state" % Auditor);

		Auditor.f_Info(fg_Format("Removed domain '{}'", Name));

		co_return 0;
	}
}
