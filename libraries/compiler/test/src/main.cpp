#include "CompilerTest.h"

using namespace emll::compiler;

int main(int argc, char* argv[])
{
	TestElementSelector();
	TestForest();

	TestBinaryPredicate(false);
	TestBinaryVector(false);
	TestBinaryVector(true);
	TestBinaryScalar();
	TestDotProduct();
	TestSum(false);
	TestSum(true);
	TestAccumulator(false);
	TestAccumulator(true);
	TestDelay();
	TestSqrt();
	TestSlidingAverage();
	TestDotProductOutput();

	TestLLVM();
	TestLLVMShiftRegister();

	return 0;
}
