// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

namespace NMib::NMeteor::NMeteorManager
{
	template <typename tf_CFormatInto>
	void CVersion::f_Format(tf_CFormatInto &o_FormatInto) const
	{
		o_FormatInto += typename tf_CFormatInto::CFormat("{}.{}.{}") << m_Major << m_Minor << m_Revision;
	}
}
