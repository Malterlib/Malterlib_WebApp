// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NWebApp::NWebAppManager
{
	template <typename tf_CStream>
	void CWebAppManagerOptions::CPackage::CRedirect::f_Stream(tf_CStream &_Stream)
	{
		_Stream % m_From;
		_Stream % m_To;
	}

	template <typename tf_CStream>
	void CWebAppManagerOptions::CPackage::CSearchReplace::f_Stream(tf_CStream &_Stream)
	{
		_Stream % m_Search;
		_Stream % m_Replace;
	}

	template <typename tf_CStream>
	void CWebAppManagerOptions::CPackage::CAlternateSource::f_Stream(tf_CStream &_Stream)
	{
		_Stream % m_Pattern;
		_Stream % m_Destination;
		_Stream % m_SearchReplace;
	}
}
