////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Machine Learning Library (EMLL)
//  File:     CodeGenerator.cpp (compile)
//  Authors:  Umesh Madan
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CodeEmitter.h"
#include "Format.h"
#include <assert.h>

namespace emll 
{
	namespace compiler 
	{
		void TestCompiler()
		{
			IREmitter llvm;
			IRModuleEmitter module(llvm, llvm.AddModule("Looper"));
			module.DeclarePrintf();

			llvm::StructType* structType = module.Struct("ShiftRegister", { ValueType::Int32, ValueType::Double });

			std::vector<double> data({3.3, 4.4, 5.5, 6.6, 7.7});
			llvm::GlobalVariable* pData = module.Constant("g_weights", data);
			llvm::GlobalVariable* pOutput = module.Global("g_output", ValueType::Double, data.size());
			llvm::GlobalVariable* pTotal = module.Global("g_total", ValueType::Double);
			llvm::GlobalVariable* pRegisters = module.Global("g_registers", structType, data.size());

			auto fnMain = module.AddMain();

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
				fnMain.Printf({ fnMain.Literal("%d, %f\n"), i,  item});
			}
			forLoop.End();

			fnMain.MemMove(pOutput, 2, 0, 3);
			fnMain.SetValueAt(pOutput, fnMain.Literal(3), fnMain.Literal(10.0));
			fnMain.SetValueAt(pOutput, fnMain.Literal(4), fnMain.Literal(20.0));

			auto pOtherTotal = module.Global("g_total", ValueType::Double);
			forLoop.Clear();
			forLoop.Begin(data.size());
			{
				auto ival = forLoop.LoadIterationVar();
				auto v = fnMain.ValueAt(pOutput, ival);

				llvm::Value* pRegisterSum = fnMain.Load(fnMain.PtrOffset(pRegisters, ival, fnMain.Literal(1)));

				fnMain.OpAndUpdate(pOtherTotal, OperatorType::AddF, v);
				fnMain.Printf({fnMain.Literal("%f, %f\n"), v, pRegisterSum});
			}
			forLoop.End();
			fnMain.Printf({fnMain.Literal("Total = %f, OtherTotal= %f\n"), fnMain.Load(pTotal), fnMain.Load(pOtherTotal)});

			fnMain.Ret();
			
			fnMain.Verify();
			module.Dump();
			module.WriteBitcodeToFile("C:\\junk\\model\\loop.bc");
		}

		template<typename ... ArgTypes>
		std::string PrintFormatPrecise(const char* format, const ArgTypes& ...args)
		{
			std::stringstream ss;
			ss.precision(17);
			utilities::PrintFormat(ss, format, args...);
			return ss.str();
		}

		CodeEmitter::CodeEmitter(DataFlowGraph& graph)
			: _graph(graph)
		{

		}

		void CodeEmitter::Assign(DataFlowNode& srcNode, DataFlowNode& destNode, const layers::Coordinate& destCoordinate)
		{
			auto assignment = EnsureVar(destNode);
			EmitAssign(assignment, srcNode.Variable(), destNode.Variable(), destCoordinate);
		}

		void CodeEmitter::Assign(ScalarVariable& srcVar, DataFlowNode& destNode, const layers::Coordinate& destCoordinate)
		{
			auto assignment = EnsureVar(destNode);
			EmitAssign(assignment, srcVar, destNode.Variable(), destCoordinate);
		}

		DataFlowNode& CodeEmitter::LinearOp(const LinearOperation& op, DataFlowNode& srcNode, const layers::Coordinate& destCoordinate)
		{
			auto& destNode = _graph.GetNode(destCoordinate);
			LinearOp(op, srcNode, destNode, destCoordinate);
			return destNode;
		}

		void CodeEmitter::LinearOp(const LinearOperation& op, DataFlowNode& srcNode, DataFlowNode& destNode, const layers::Coordinate& destCoordinate)
		{
			auto assignment = EnsureVar(destNode);
			EmitLinearOp(op, assignment, srcNode.Variable(), destNode.Variable(), destCoordinate);
		}

		CodeEmitter::Assignment CodeEmitter::EnsureVar(DataFlowNode& node)
		{
			if (node.IsInitialized())
			{
				return CodeEmitter::Assignment::IncrementBy;
			}

			if (node.HasFixedVariableName())
			{
				return CodeEmitter::Assignment::Set;
			}

			bool isNovel = _varStack.IsTopNovel();
			node.SetTempVariableIndex(_varStack.Pop());

			if (isNovel)
			{
				return CodeEmitter::Assignment::Declare;
			}

			return CodeEmitter::Assignment::Reset;
		}

		void CodeEmitter::ReleaseVar(DataFlowNode& node)
		{
			_varStack.Push(node.GetTempVariableIndex());
		}

		//-------------------------------------------------------
		//
		// CEmitter
		//
		//-------------------------------------------------------

		CCodeEmitter::CCodeEmitter(DataFlowGraph& graph, std::ostream& os)
			: CodeEmitter(graph), _os(os)
		{
		}

		void CCodeEmitter::BeginLinear(const std::string& functionName, const std::string& inputVarName, uint64_t inputCount, const std::string& outputVarName, const layers::CoordinateList& outputs)
		{
			auto str = "// New Compiler \n// Input dimension: %\n// Output dimension: %\n// Output coordinates:";
			utilities::PrintFormat(_os, str, inputCount, outputs.Size());
			for (uint64_t i = 0; i < outputs.Size(); ++i)
			{
				_os << ' ' << outputs[i];
			}
			_os << "\n";
			//
			// Function Declaration
			//
			utilities::PrintFormat(_os, "void %(const double* %, double* %)\n{\n", functionName, inputVarName, outputVarName);
		}

		void CCodeEmitter::EndLinear()
		{
			_os << "}\n";
		}

		void CCodeEmitter::EmitAssign(Assignment assignment, ScalarVariable& srcVar, ScalarVariable& destVar, const layers::Coordinate& destCoordinate)
		{
			EnsureEmittedName(srcVar);
			EnsureEmittedName(destVar);

			const char* format = ToString(assignment);
			utilities::PrintFormat(_os, format, destVar.EmittedName(), srcVar.EmittedName(), destCoordinate.GetLayerIndex(), destCoordinate.GetElementIndex());
		}

		void CCodeEmitter::EmitLinearOp(const LinearOperation& op, Assignment assignment, ScalarVariable& srcVar, ScalarVariable& destVar, const layers::Coordinate& destCoordinate)
		{
			EnsureEmittedName(srcVar);
			EnsureEmittedName(destVar);

			const char* format = ToString(assignment);
			std::string rhs = ToString(op, srcVar.EmittedName());
			utilities::PrintFormat(_os, format, destVar.EmittedName(), rhs, destCoordinate.GetLayerIndex(), destCoordinate.GetElementIndex());
		}

		void CCodeEmitter::EnsureEmittedName(ScalarVariable& var)
		{
			if (!var.HasEmittedName())
			{
				if (var.IsArrayElement())
				{
					var.SetEmittedName(var.Name() + "[" + std::to_string(var.ElementOffset()) + "]");
				}
				else
				{
					var.SetEmittedName(var.Name());
				}
			}
		}

		const char* CCodeEmitter::ToString(CodeEmitter::Assignment assignment)
		{
			const char* format = nullptr;
			switch (assignment)
			{
				default:
					throw new CodeEmitterException(CodeEmitterError::NotSupported);
					break;
				case Assignment::Declare:
					format = "    double % = %; // coordinate (%,%), allocating new temporary variable\n";
					break;
				case Assignment::Set:
					format = "    % = %; // coordinate (%,%)\n";
					break;
				case Assignment::IncrementBy:
					format = "    % += %; // coordinate (%,%)\n";
					break;
				case Assignment::Reset:
					format = "    % = %; // coordinate (%,%), reassigning temporary variable\n";
					break;
			}
			return format;
		}

		std::string CCodeEmitter::ToString(const LinearOperation& op, const std::string& sourceVar)
		{
			double a = op.MultiplyBy();
			double b = op.IncrementBy();
			if (b == 0)
			{
				if (a != 1)
				{
					return PrintFormatPrecise("% * %", a, sourceVar);
				}
				else
				{
					return sourceVar;
				}
			}
			else
			{
				if (a == 0)
				{
					return std::to_string(b);
				}
				else if (a == 1)
				{
					return PrintFormatPrecise("% + %", sourceVar, b);
				}
				else
				{
					return PrintFormatPrecise("% * % + %", a, sourceVar, b);
				}
			}
		}


		//-------------------------------------------------------
		//
		// IRCodeEmitter
		//
		//-------------------------------------------------------

		IRCodeEmitter::IRCodeEmitter(DataFlowGraph& graph, std::ostream& os)
			: CodeEmitter(graph), 
			_os(os), 
			_module(_emitter, "EMLL")
		{
		}

		void IRCodeEmitter::Begin()
		{
		}

		void IRCodeEmitter::End()
		{
			_module.WriteAsmToStream(_os);
		}

		void IRCodeEmitter::BeginLinear(const std::string& functionName, const std::string& inputVarName, uint64_t inputCount, const std::string& outputVarName, const layers::CoordinateList& outputs)
		{
			NamedValueTypeList fnArgs;
			fnArgs.init({ {inputVarName, ValueType::PDouble}, { outputVarName, ValueType::PDouble }});
			_fn = _module.Function(functionName, ValueType::Void, fnArgs, true);	
			//
			// Register input and out as variables for the function body to use
			//
			auto args = _fn.Args().begin();
			auto arg = &(*args);						
			_variables.Set(inputVarName, arg);
			
			args++;
			arg = &(*args);
			_variables.Set(outputVarName, arg);
		}

		void IRCodeEmitter::EndLinear()
		{
			_fn.Ret();
			_fn.Verify();
		}

		void IRCodeEmitter::EmitTest(const std::string& fnName, int featureCount, int outputCount, double startValue)
		{			
			_module.DeclarePrintf();

			auto fn = _module.AddMain();
			llvm::Value* features = fn.Var(ValueType::Double, featureCount);
			for (int i = 0; i < featureCount; ++i)
			{
				auto value = startValue * (i + 1);
				fn.SetValueAtA(features, i, fn.Literal(value));
			}
			llvm::Value* output = fn.Var(ValueType::Double, outputCount);
			fn.Call(fnName, { features, output });

			for (int i = 0; i < outputCount; ++i)
			{
				llvm::Value* result = fn.ValueAtA(output, i);
				fn.Call("printf", { fn.Literal("Result = %f\n"), result });
			}
			fn.Ret();
			fn.Verify();
		}


		void IRCodeEmitter::EmitAssign(Assignment assignment, ScalarVariable& srcVar, ScalarVariable& destVar, const layers::Coordinate& destCoordinate)
		{
			llvm::Value* pSrc = LoadVar(srcVar);
			switch (assignment)
			{
				default:
					throw new CodeEmitterException(CodeEmitterError::NotSupported);
					break;
				case Assignment::Declare:
				case Assignment::Set:
				case Assignment::Reset:
					Store(destVar, pSrc);
					break;
				case Assignment::IncrementBy:
					Increment(destVar, pSrc);
					break;
			}
		}

		void IRCodeEmitter::EmitLinearOp(const LinearOperation& op, Assignment assignment, ScalarVariable& srcVar, ScalarVariable& destVar, const layers::Coordinate& destCoordinate)
		{
			llvm::Value* pResult = Emit(srcVar, op);
			switch (assignment)
			{
				default:
					throw new CodeEmitterException(CodeEmitterError::NotSupported);
					break;
				case Assignment::Declare:
				case Assignment::Set:
				case Assignment::Reset:
					Store(destVar, pResult);
					break;
				case Assignment::IncrementBy:
					Increment(destVar, pResult);
					break;
			}
		}

		llvm::Value* IRCodeEmitter::Emit(ScalarVariable& srcVar, const LinearOperation& op)
		{
			double a = op.MultiplyBy();
			double b = op.IncrementBy();
			llvm::Value* pSrc = LoadVar(srcVar);
			if (b == 0)
			{
				if (a != 1)
				{
					return _fn.Op(OperatorType::MultiplyF, _fn.Literal(a), pSrc);
				}
				else
				{
					return pSrc;
				}
			}
			else
			{
				if (a == 0)
				{
					return _fn.Literal(b);
				}
				else if (a == 1)
				{
					return _fn.Op(OperatorType::AddF, _fn.Literal(b), pSrc);
				}
				else
				{
					return _fn.Op(OperatorType::AddF,
								_fn.Op(OperatorType::MultiplyF, _fn.Literal(a), pSrc),
								_fn.Literal(b));
				}
			}
		}

		void IRCodeEmitter::Store(ScalarVariable& destVar, llvm::Value* pValue)
		{
			llvm::Value *pDest = EnsureVar(destVar);
			if (destVar.IsArrayElement())
			{
				_fn.SetValueAtA(pDest, destVar.ElementOffset(), pValue);
			}
			else
			{
				_fn.Store(pDest, pValue);
			}
		}

		void IRCodeEmitter::Increment(ScalarVariable& destVar, llvm::Value* pValue)
		{
			// Load variables into registers
			llvm::Value *pDest = LoadVar(destVar);

			// pSum is in a register...
			llvm::Value* pSum = _fn.Op(OperatorType::AddF, pValue, pDest);

			Store(destVar, pSum);
		}

		llvm::Value* IRCodeEmitter::EnsureVar(ScalarVariable& var)
		{
			auto name = var.Name();
			llvm::Value* pValue = _variables.Get(name);			
			if (pValue == nullptr)
			{
				if (var.IsArrayElement())
				{
					throw new CodeEmitterException(CodeEmitterError::ArrayMustBeAllocated);
				}
				// We currently assume that all variables are doubles
				pValue = _fn.Var(ValueType::Double, name);
				_variables.Set(name, pValue);
			}
			return pValue;
		}

		llvm::Value* IRCodeEmitter::LoadVar(ScalarVariable& var)
		{
			llvm::Value *pValue = EnsureVar(var);
			if (var.IsArrayElement())
			{
				pValue = _fn.ValueAtA(pValue, var.ElementOffset());
			}
			else
			{
				// All our temporary variables are stack variables, so we must load them
				pValue = _fn.Load(pValue);
			}
			return pValue;
		}
	}
}

