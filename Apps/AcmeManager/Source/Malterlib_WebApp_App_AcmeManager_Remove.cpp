// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Cryptography/RandomID>
#include <Mib/Concurrency/LogError>

#include "Malterlib_WebApp_App_AcmeManager.h"

namespace NMib::NWebApp::NAcmeManager
{
	TCFuture<uint32> CAcmeManagerActor::fp_CommandLine_DomainRemove(CEJsonSorted const _Params, NStorage::TCSharedPointer<CCommandLineControl> _pCommandLine)
	{
		auto Auditor = f_Auditor();

		CStr Name = _Params["Domain"].f_String();

		auto *pDomain = mp_Domains.f_FindEqual(Name);
		if (!pDomain)
			co_return Auditor.f_Exception(fg_Format("No such domain '{}'", Name));

		auto DestroyFuture = fg_Move(pDomain->m_UpdateDomainSequencer).f_Destroy();
		mp_Domains.f_Remove(pDomain);
		co_await fg_Move(DestroyFuture).f_Wrap() > fg_LogError("AcmeManager", "Failed to destroy update domain sequencer");

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
