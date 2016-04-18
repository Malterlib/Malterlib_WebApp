
#include <Mib/Core/Core>
#include <Mib/Test/Test>

class CMeteor_Tests : public NMib::NTest::CTest
{
public:
	
	void f_DoTests()
	{
	}
};

DMibTestRegister(CMeteor_Tests, Malterlib::Meteor);
