// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#include "Malterlib_WebApp_App_WebAppManager_Helpers.h"

namespace NMib::NWebApp::NWebAppManager
{
	CUser::CUser(CStr const &_UserName, CStr const &_GroupName)
		: m_UserName(_UserName)
		, m_GroupName(_GroupName)
	{
	}

	void CUser::f_Format(NStr::CStrAggregate &o_Str) const
	{
		o_Str += "[{}, {}]"_f << m_UserName << m_GroupName;
	}

	CVersion::CVersion(uint32 _Major, uint32 _Minor, uint32 _Revision)
		: m_Major(_Major)
		, m_Minor(_Minor)
		, m_Revision(_Revision)
	{
	}

	CVersion::CVersion()
	{
	}

	bool CVersion::operator < (CVersion const &_Right) const
	{
		return fg_TupleReferences(m_Major, m_Minor, m_Revision) < fg_TupleReferences(_Right.m_Major, _Right.m_Minor, _Right.m_Revision);
	}

	CToolLaunch::CToolLaunch()
		: m_pDestroyed(fg_Construct(false))
	{
	}

	CToolLaunch::~CToolLaunch()
	{
		*m_pDestroyed = true;
	}
}
