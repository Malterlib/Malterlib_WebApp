// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

namespace NMib::NWebApp::NWebAppManager
{
	template <typename tf_CFormatInto>
	void CVersion::f_Format(tf_CFormatInto &o_FormatInto) const
	{
		o_FormatInto += typename tf_CFormatInto::CFormat("{}.{}.{}") << m_Major << m_Minor << m_Revision;
	}
}
