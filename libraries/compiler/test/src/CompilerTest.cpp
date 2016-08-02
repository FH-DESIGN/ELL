#include <iostream>
#include <string>
#include <ostream>

#include "CompilerTest.h"
#include "IRInclude.h"
#include "IRCompiler.h"
#include "ScalarVar.h"
#include "VectorVar.h"
#include "testing.h"

#include "ForestNode.h"

using namespace emll::compiler;

void NodePrinter(const model::Node& node)
{
	std::cout << "node_" << node.GetId() << " = " << node.GetRuntimeTypeName() << std::endl;
}

void TestLLVMShiftRegister()
{
	IREmitter llvm;
	IRModuleEmitter module(llvm, llvm.AddModule("Shifter"));
	module.DeclarePrintf();

	std::vector<double> data({ 1.1, 2.1, 3.1, 4.1, 5.1});
	std::vector<double> newData1({ 1.2, 2.2 });
	std::vector<double> newData2({ 3.2, 4.2 });

	auto fn = module.AddMain();
	llvm::GlobalVariable* pRegister = module.Global("g_shiftRegister", data);
	llvm::Value* c1= module.Constant("c_1", newData1);
	llvm::Value* c2 = module.Constant("c_2", newData2);

	fn.Print("Begin\n");
	fn.PrintForEach("%f\n", pRegister, data.size());
	fn.Print("Shift 1\n");
	fn.ShiftAndUpdate<double>(pRegister, data.size(), newData1.size(), c1);
	fn.PrintForEach("%f\n", pRegister, data.size());
	fn.Print("Shift 2\n");
	fn.ShiftAndUpdate<double>(pRegister, data.size(), newData2.size(), c2);
	fn.PrintForEach("%f\n", pRegister, data.size());
	fn.Ret();
	fn.Verify();

	module.Dump();
	module.WriteBitcodeToFile("C:\\junk\\model\\shift.bc");
	}

void TestLLVM()
{
	IREmitter llvm;
	IRModuleEmitter module(llvm, llvm.AddModule("Looper"));
	module.DeclarePrintf();

	llvm::StructType* structType = module.Struct("ShiftRegister", { ValueType::Int32, ValueType::Double });

	std::vector<double> data({ 3.3, 4.4, 5.5, 6.6, 7.7 });
	llvm::GlobalVariable* pData = module.Constant("g_weights", data);
	llvm::GlobalVariable* pOutput = module.Global(ValueType::Double, "g_output", data.size());
	llvm::GlobalVariable* pTotal = module.Global(ValueType::Double, "g_total");
	llvm::GlobalVariable* pRegisters = module.Global("g_registers", structType, data.size());

	auto fnMain = module.AddMain();

	auto vectorResult = fnMain.DotProductF(data.size(), fnMain.Ptr(pData), fnMain.Ptr(pData));
	fnMain.Printf({ fnMain.Literal("DOT %f\n"), fnMain.Load(vectorResult)});

	IRForLoopEmitter forLoop(fnMain);
	auto pBodyBlock = forLoop.Begin(data.size());
	{
		auto printBlock = fnMain.BlockAfter(pBodyBlock, "PrintBlock");
		fnMain.Branch(printBlock);
		fnMain.CurrentBlock(printBlock);

		auto i = forLoop.LoadIterationVar();
		auto item = fnMain.ValueAt(pData, i);
		auto sum = fnMain.Op(OperatorType::AddF, fnMain.Literal(0.3), item);
		fnMain.SetValueAt(pOutput, i, sum);
		fnMain.OpAndUpdate(pTotal, OperatorType::AddF, sum);

		llvm::Value* pRegisterSum = fnMain.PtrOffset(pRegisters, i, fnMain.Literal(1));
		fnMain.Store(pRegisterSum, sum);

		//auto itemInt = fnMain.CastFloatToInt(item);
		IRIfEmitter ife(fnMain);
		ife.If(ComparisonType::LtF, item, fnMain.Literal(5.7));
		{
			fnMain.Print("First IF!\n");
		}
		ife.If(ComparisonType::EqF, item, fnMain.Literal(6.6));
		{
			fnMain.Print("Second If!\n");
		}
		ife.Else();
		{
			fnMain.Print("Else\n");
		}
		ife.End();
		fnMain.Printf({ fnMain.Literal("%d, %f\n"), i,  item });
	}
	forLoop.End();

	fnMain.SetValueAt(pOutput, fnMain.Literal(3), fnMain.Literal(10.0));
	fnMain.SetValueAt(pOutput, fnMain.Literal(4), fnMain.Literal(20.0));

	auto pOtherTotal = module.Global(ValueType::Double, "g_total");
	forLoop.Clear();
	forLoop.Begin(data.size());
	{
		auto ival = forLoop.LoadIterationVar();
		auto v = fnMain.ValueAt(pOutput, ival);

		llvm::Value* pRegisterSum = fnMain.Load(fnMain.PtrOffset(pRegisters, ival, fnMain.Literal(1)));

		fnMain.OpAndUpdate(pOtherTotal, OperatorType::AddF, v);
		fnMain.Printf({ fnMain.Literal("%f, %f\n"), v, pRegisterSum });
	}
	forLoop.End();
	fnMain.Printf({ fnMain.Literal("Total = %f, OtherTotal= %f\n"), fnMain.Load(pTotal), fnMain.Load(pOtherTotal) });

	fnMain.Ret();

	fnMain.Verify();
	module.Dump();
	module.WriteBitcodeToFile("C:\\junk\\model\\loop.bc");
}


ModelBuilder::ModelBuilder()
{
}

ModelBuilder::ModelBuilder(const ModelBuilder& src)
	: _model(src._model)
{
}

ModelBuilder::ModelBuilder(ModelBuilder&& src)
	: _model(std::move(src._model))
{
}

template<typename T>
model::InputNode<T>* ModelBuilder::Inputs(size_t count)
{
	return _model.AddNode<model::InputNode<T>>(count);
}

template<typename T>
model::InputNode<T>* ModelBuilder::Inputs(std::vector<T>& values)
{
	auto node = Inputs<T>(values.size());
	node->SetInput(values);
	return node;
}

template<typename T>
model::OutputNode<T>* ModelBuilder::Outputs(const model::OutputPort<T>& x)
{
	return _model.AddNode<model::OutputNode<T>>(x);
}

template<typename T>
nodes::BinaryOperationNode<T>* ModelBuilder::Add(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryOperationNode<T>>(x, y, nodes::BinaryOperationNode<T>::OperationType::add);
}

template<typename T>
nodes::BinaryOperationNode<T>* ModelBuilder::Subtract(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryOperationNode<T>>(x, y, nodes::BinaryOperationNode<T>::OperationType::subtract);
}

template<typename T>
nodes::BinaryOperationNode<T>* ModelBuilder::Multiply(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryOperationNode<T>>(x, y, nodes::BinaryOperationNode<T>::OperationType::coordinatewiseMultiply);
}

template<typename T>
nodes::BinaryOperationNode<T>* ModelBuilder::Divide(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryOperationNode<T>>(x, y, nodes::BinaryOperationNode<T>::OperationType::divide);
}

template<typename T>
nodes::DotProductNode<T>* ModelBuilder::DotProduct(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::DotProductNode<T>>(x, y);
}

template<typename T>
nodes::BinaryPredicateNode<T>* ModelBuilder::Equals(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryPredicateNode<T>>(x, y, nodes::BinaryPredicateNode<T>::PredicateType::equal);
}

template<typename T>
nodes::BinaryPredicateNode<T>* ModelBuilder::Lt(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryPredicateNode<T>>(x, y, nodes::BinaryPredicateNode<T>::PredicateType::less);
}

template<typename T>
nodes::BinaryPredicateNode<T>* ModelBuilder::Gt(const model::OutputPort<T>& x, const model::OutputPort<T>& y)
{
	return _model.AddNode<nodes::BinaryPredicateNode<T>>(x, y, nodes::BinaryPredicateNode<T>::PredicateType::greater);
}

template<typename T, typename S>
nodes::ElementSelectorNode<T, S>* ModelBuilder::Select(const model::OutputPort<T>& elts, const model::OutputPort<S>& selector)
{
	auto node = _model.AddNode<nodes::ElementSelectorNode<T, S>>(elts, selector);
	return node;
}

template<typename T>
nodes::UnaryOperationNode<T>* ModelBuilder::Sqrt(const model::OutputPort<T>& x)
{
	return _model.AddNode<nodes::UnaryOperationNode<T>>(x, nodes::UnaryOperationNode<T>::OperationType::sqrt);
}

template<typename T>
nodes::SumNode<T>* ModelBuilder::Sum(const model::OutputPort<T>& x)
{
	return _model.AddNode<nodes::SumNode<T>>(x);
}

template<typename T>
nodes::DelayNode<T>* ModelBuilder::Delay(const model::OutputPort<T>& x, size_t windowSize)
{
	return _model.AddNode<nodes::DelayNode<T>>(x, windowSize);
}

template<typename T>
nodes::AccumulatorNode<T>* ModelBuilder::Accumulate(const model::OutputPort<T>& x)
{
	return _model.AddNode<nodes::AccumulatorNode<T>>(x);
}

template<typename T>
nodes::ConstantNode<T>* ModelBuilder::Constant(const T value)
{
	return _model.AddNode<nodes::ConstantNode<T>>(value);
}

template<typename T>
nodes::ConstantNode<T>* ModelBuilder::Constant(const std::vector<T>& values)
{
	auto *pNode = _model.AddNode<nodes::ConstantNode<T>>(values);
	// Work around a bug. Make sure literal values are propagated to outputs
	_model.ComputeNodeOutput<T>(pNode->output);
	return pNode;
}

template<typename T>
model::OutputPort<T>* ModelBuilder::GetOutputPort(model::Node* pNode, size_t portIndex)
{
	auto pPort = pNode->GetOutputPorts()[portIndex];
	return static_cast<model::OutputPort<T>*>(pPort);
}

model::Model InitTestModelBinOp()
{
	ModelBuilder builder;
	std::vector<double> data = { 10, 100, 1000, 10000 };

	auto input = builder.Inputs<double>(data);
	auto c = builder.Constant<double>({ 5, 50, 500, 5000 });

	nodes::BinaryOperationNode<double>* multNode = builder.Multiply<double>(input->output, c->output);
	multNode = builder.Multiply<double>(multNode->output, c->output);	
	multNode = builder.Multiply<double>(multNode->output, c->output);
	auto addNode = builder.Add<double>(input->output, multNode->output);
	addNode = builder.Add<double>(c->output, addNode->output);
	multNode = builder.Multiply<double>(addNode->output, c->output);
	multNode = builder.Multiply<double>(multNode->output, c->output);
	addNode = builder.Add<double>(c->output, multNode->output);

	return builder.Model;
}

model::Model InitTestModelSimple()
{
	ModelBuilder mb;
	auto input = mb.Inputs<double>(2);
	auto c = mb.Constant<double>({ 5, 3});

	nodes::BinaryOperationNode<double>* multNode = mb.Multiply<double>(input->output, c->output);
	auto addNode = mb.Add<double>(c->output, multNode->output);
	addNode = mb.Add<double>(c->output, addNode->output);
	return mb.Model;
}

void TestBinaryVector(bool expanded)
{
	std::vector<double> data = { 5, 10, 15, 20};
	std::vector<double> data2 = { 4, 4, 4, 4};

	ModelBuilder mb;

	auto input1 = mb.Inputs<double>(4);
	auto c1 = mb.Constant<double>(data);
	auto c2 = mb.Constant<double>(data2);

	auto bop = mb.Add(c1->output, input1->output);
	auto bop2 = mb.Multiply(bop->output, c2->output);
	auto output = mb.Outputs<double>(bop2->output);

	IRCompiler compiler("EMLL");
	compiler.Settings().ShouldUnrollLoops() = expanded;
	compiler.CompileModel("TestBinaryVector", mb.Model);
	compiler.DebugDump();
}

void TestBinaryScalar()
{
	std::vector<double> data = {5};
	std::vector<double> data2 = {4};

	ModelBuilder mb;

	auto input1 = mb.Inputs<double>(1);
	auto c1 = mb.Constant<double>(data);
	auto c2 = mb.Constant<double>(data2);

	auto bop = mb.Add(c1->output, input1->output);
	//auto bop2 = mb.Multiply(bop->output, c2->output);
	auto output = mb.Outputs<double>(bop->output);

	IRCompiler compiler("EMLL");
	compiler.CompileModel("TestBinaryScalar", mb.Model);
	compiler.DebugDump();
}

void TestDotProduct(CompilerSettings& settings)
{
	std::vector<double> data = { 5, 10, 15, 20 };

	ModelBuilder mb;
	auto c1 = mb.Constant<double>(data);
	auto input1 = mb.Inputs<double>(4);
	auto dotProduct = mb.DotProduct<double>(c1->output, input1->output);
	auto output = mb.Outputs<double>(dotProduct->output);

	IRCompiler compiler("EMLL");
	compiler.Settings() = settings;
	compiler.CompileModel("TestDotProduct", mb.Model);
	compiler.DebugDump();
}

void TestDotProduct()
{
	CompilerSettings settings;
	
	settings.ShouldUnrollLoops() = false;
	settings.ShouldInlineOperators() = true;
	TestDotProduct(settings);

	settings.ShouldUnrollLoops() = true;
	settings.ShouldInlineOperators() = true;
	TestDotProduct(settings);

	settings.ShouldUnrollLoops() = false;
	settings.ShouldInlineOperators() = false;
	TestDotProduct(settings);
}

void TestSum(bool expanded)
{
	std::vector<double> data = { 5, 10, 15, 20 };

	ModelBuilder mb;
	auto c1 = mb.Constant<double>(data);
	auto input1 = mb.Inputs<double>(4);
	auto product = mb.Multiply<double>(c1->output, input1->output);
	auto sum = mb.Sum<double>(product->output);
	auto output = mb.Outputs<double>(sum->output);

	IRCompiler compiler("EMLL");
	compiler.Settings().ShouldUnrollLoops() = expanded;
	compiler.CompileModel("TestSum", mb.Model);
	compiler.DebugDump();
}

void TestAccumulator(bool expanded)
{
	std::vector<double> data = { 5, 10, 15, 20 };

	ModelBuilder mb;
	auto c1 = mb.Constant<double>(data);
	auto input1 = mb.Inputs<double>(4);
	auto product = mb.Multiply<double>(c1->output, input1->output);
	auto accumulate = mb.Accumulate<double>(product->output);
	auto output = mb.Outputs<double>(accumulate->output);

	IRCompiler compiler("EMLL");
	compiler.Settings().ShouldUnrollLoops() = expanded;
	compiler.CompileModel("TestAccumulator", mb.Model);
	compiler.DebugDump();
}

void TestDelay()
{
	ModelBuilder mb;
	auto input1 = mb.Inputs<double>(4);
	auto delay = mb.Delay<double>(input1->output, 3);
	auto output = mb.Outputs<double>(delay->output);

	IRCompiler compiler("EMLL");
	compiler.CompileModel("TestDelay", mb.Model);
	compiler.DebugDump();
}

void TestSqrt()
{
	ModelBuilder mb;
	auto input1 = mb.Inputs<double>(1);
	auto sqrt = mb.Sqrt<double>(input1->output);
	auto output = mb.Outputs<double>(sqrt->output);

	IRCompiler compiler("EMLL");
	compiler.CompileModel("TestSqrt", mb.Model);
	compiler.DebugDump();
}

void TestBinaryPredicate(bool expanded)
{
	std::vector<double> data = { 5 };

	ModelBuilder mb;
	auto input1 = mb.Inputs<double>(data.size());
	auto c1 = mb.Constant<double>(data);
	auto eq = mb.Equals(input1->output, c1->output);
	auto output = mb.Outputs<bool>(eq->output);

	IRCompiler compiler("EMLL");
	compiler.CompileModel("TestPredicate", mb.Model);
	compiler.DebugDump();
}

void TestElementSelector()
{
	ModelBuilder mb;

	std::vector<double> data = { 5, 10 };
	auto c1 = mb.Constant<bool>(true);
	auto input1 = mb.Inputs<double>(data.size());
	auto selector = mb.Select<double, bool>(input1->output, c1->output);
	//selector->output.SetOutput(0);
	auto output = mb.Outputs<double>(*mb.GetOutputPort<double>(selector, 0));

	IRCompiler compiler("EMLL");
	compiler.CompileModel("TestElementSelector", mb.Model);
	compiler.DebugDump();
}

void TestSlidingAverage()
{
	ModelBuilder mb;
	auto dim = mb.Constant<double>(4);
	auto input1 = mb.Inputs<double>(4);
	auto delay = mb.Delay<double>(input1->output, 2);
	auto sum = mb.Sum<double>(delay->output);
	auto avg = mb.Divide<double>(sum->output, dim->output);
	auto output = mb.Outputs<double>(avg->output);

	IRCompiler compiler("EMLL");
	compiler.CompileModel("TestSlidingAverage", mb.Model);

	auto& module = compiler.Module();
	module.DeclarePrintf();
	auto fnMain = module.AddMain();
	std::vector<double> data = { 5, 10, 15, 20 };
	llvm::Value* pData = module.Constant("c_data", data);
	llvm::Value* pResult = fnMain.Var(ValueType::Double, 1);
	fnMain.Call("TestSlidingAverage", {fnMain.PtrOffset(pData, 0), fnMain.PtrOffset(pResult, 0)});
	fnMain.PrintForEach("%f\n", pResult, 1);
	fnMain.Call("TestSlidingAverage", { fnMain.PtrOffset(pData, 0), fnMain.PtrOffset(pResult, 0) });
	fnMain.PrintForEach("%f\n", pResult, 1);
	fnMain.Call("TestSlidingAverage", { fnMain.PtrOffset(pData, 0), fnMain.PtrOffset(pResult, 0) });
	fnMain.PrintForEach("%f\n", pResult, 1);
	fnMain.Ret();
	fnMain.Verify();

	compiler.DebugDump();
	module.WriteBitcodeToFile("C:\\junk\\model\\avg.bc");
}

void TestDotProductOutput()
{
	CompilerSettings settings;
	std::vector<double> data = { 5, 10, 15, 20 };

	ModelBuilder mb;
	auto c1 = mb.Constant<double>(data);
	auto input1 = mb.Inputs<double>(4);
	auto dotProduct = mb.DotProduct<double>(c1->output, input1->output);
	auto output = mb.Outputs<double>(dotProduct->output);

	IRCompiler compiler("EMLL");
	compiler.Settings().ShouldInlineOperators() = false;
	compiler.CompileModel("TestDotProduct", mb.Model);

	auto& module = compiler.Module();
	module.DeclarePrintf();
	auto fnMain = module.AddMain();
	llvm::Value* pData = module.Constant("c_data", data);
	llvm::Value* pResult = fnMain.Var(ValueType::Double, 1);
	fnMain.Call("TestDotProduct", { fnMain.PtrOffset(pData, 0), fnMain.PtrOffset(pResult, 0) });
	fnMain.PrintForEach("%f\n", pResult, 1);
	fnMain.Ret();
	fnMain.Verify();

	compiler.DebugDump();
	module.WriteBitcodeToFile("C:\\junk\\model\\dot.bc");
}

model::Model MakeForest()
{
	// define some abbreviations
	using SplitAction = predictors::SimpleForestPredictor::SplitAction;
	using SplitRule = predictors::SingleElementThresholdRule;
	using EdgePredictorVector = std::vector<predictors::ConstantPredictor>;
	using NodeId = predictors::SimpleForestPredictor::SplittableNodeId;

	// build a forest
	predictors::SimpleForestPredictor forest;
	auto root = forest.Split(SplitAction{ forest.GetNewRootId(), SplitRule{ 0, 0.3 }, EdgePredictorVector{ -1.0, 1.0 } });
	forest.Split(SplitAction{ forest.GetChildId(root, 0), SplitRule{ 1, 0.6 }, EdgePredictorVector{ -2.0, 2.0 } });
	forest.Split(SplitAction{ forest.GetChildId(root, 1), SplitRule{ 2, 0.9 }, EdgePredictorVector{ -4.0, 4.0 } });
	forest.Split(SplitAction{ forest.GetNewRootId(), SplitRule{ 0, 0.2 }, EdgePredictorVector{ -3.0, 3.0 } });

	// build the model
	model::Model model;
	auto inputNode = model.AddNode<model::InputNode<double>>(3);
	auto simpleForestNode = model.AddNode<nodes::SimpleForestNode>(inputNode->output, forest);

	// refine
	model::TransformContext context;
	model::ModelTransformer transformer;
	auto refinedModel = transformer.RefineModel(model, context);
	/*
	// check equivalence
	auto refinedInputNode = transformer.GetCorrespondingInputNode(inputNode);
	auto refinedOutputPort = transformer.GetCorrespondingOutputPort(simpleForestNode->prediction);

	inputNode->SetInput({ 0.2, 0.5, 0.0 });
	refinedInputNode->SetInput({ 0.2, 0.5, 0.0 });
	auto outputValue = model.ComputeNodeOutput(simpleForestNode->prediction)[0];
	auto refinedOutputValue = refinedModel.ComputeNodeOutput(*refinedOutputPort)[0];

	//  expected output is -3.0
	testing::ProcessTest("Testing SimpleForestNode refine", testing::IsEqual(outputValue, refinedOutputValue));
	*/
	return refinedModel;
}

void TestForest()
{
	model::Model model = MakeForest();

	IRCompiler compiler;
	compiler.CompileModel("TestForest", model);
	compiler.DebugDump();
}

