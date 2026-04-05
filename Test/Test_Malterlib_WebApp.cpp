// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Test/Test>

class CWebApp_Tests : public NMib::NTest::CTest
{
public:

	void f_DoTests()
	{
	}
};

DMibTestRegister(CWebApp_Tests, Malterlib::WebApp);
