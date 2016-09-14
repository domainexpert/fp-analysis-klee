/*
 * SymbolicError.cpp
 *
 *  Created on: 18 Aug 2016
 *      Author: himeshi
 */
#include "SymbolicError.h"
#include "llvm/IR/Instructions.h"

using namespace klee;

ref<Expr> SymbolicError::getError(Executor *executor, Expr *value) {
	ref<Expr> errorAmount = valueErrorMap[value];

	if (!errorAmount.get()) {
		if (llvm::isa<ConcatExpr>(*value)) {
			ConcatExpr *concatExpr = llvm::dyn_cast<ConcatExpr>(value);
			const Array *concatArray = llvm::dyn_cast<ReadExpr>(
					concatExpr->getLeft())->updates.root;
			const Array *errorArray = arrayErrorArrayMap[concatArray];
			if (!errorArray) {
				std::string errorName(
						"_fractional_error_"
								+ llvm::dyn_cast<ReadExpr>(
										concatExpr->getLeft())->updates.root->name);
				const Array *newErrorArray = errorArrayCache.CreateArray(
						errorName, Expr::Int8);
				UpdateList ul(newErrorArray, 0);
				arrayErrorArrayMap[concatArray] = newErrorArray;
				ref<Expr> newReadExpr = ReadExpr::create(ul,
						ConstantExpr::alloc(0, Expr::Int8));
				valueErrorMap[value] = newReadExpr;
				return newReadExpr;
			}
			UpdateList ul(errorArray, 0);
			ref<Expr> newReadExpr = ReadExpr::create(ul,
					ConstantExpr::alloc(0, Expr::Int8));
			valueErrorMap[value] = newReadExpr;
			return newReadExpr;

		} else if (llvm::isa<ReadExpr>(*value)) {
			ReadExpr *readExpr = llvm::dyn_cast<ReadExpr>(value);
			const Array *readArray = readExpr->updates.root;
			const Array *errorArray = arrayErrorArrayMap[readArray];
			if (!errorArray) {
				std::string errorName(
						"_fractional_error_" + readExpr->updates.root->name);
				const Array *newErrorArray = errorArrayCache.CreateArray(
						errorName, Expr::Int8);
				UpdateList ul(newErrorArray, 0);
				arrayErrorArrayMap[readArray] = newErrorArray;
				ref<Expr> newReadExpr = ReadExpr::create(ul,
						ConstantExpr::alloc(0, Expr::Int8));
				valueErrorMap[value] = newReadExpr;
				return newReadExpr;
			}
			UpdateList ul(errorArray, 0);
			ref<Expr> newReadExpr = ReadExpr::create(ul,
					ConstantExpr::alloc(0, Expr::Int8));
			valueErrorMap[value] = newReadExpr;
			return newReadExpr;

		} else if (llvm::isa<ConstantExpr>(value)) {
			return ConstantExpr::alloc(0, Expr::Int8);

		} else {
			assert(!"malformed expression");
		}
	}

	return errorAmount;
}

SymbolicError::~SymbolicError() {
}

ref<Expr> SymbolicError::getCurrentError() {
	return currentError;
}

void SymbolicError::propagateError(Executor *executor, llvm::Instruction *instr,
		ref<Expr> result, std::vector<ref<Expr> > &arguments) {
	switch (instr->getOpcode()) {
	case llvm::Instruction::Add: {
		ref<Expr> lError = valueErrorMap[arguments[0].get()];
		ref<Expr> rError = valueErrorMap[arguments[1].get()];
		if (!lError.get()) {
			lError = getError(executor, arguments[0].get());
		}
		if (!rError.get()) {
			rError = getError(executor, arguments[1].get());
		}
		ref<Expr> extendedLeft = ZExtExpr::create(lError,
				arguments[0]->getWidth());
		ref<Expr> extendedRight = ZExtExpr::create(rError,
				arguments[1]->getWidth());
		ref<Expr> errorLeft = MulExpr::create(extendedLeft.get(),
				arguments[0].get());
		ref<Expr> errorRight = MulExpr::create(extendedRight.get(),
				arguments[1].get());
		ref<Expr> resultError = AddExpr::create(errorLeft, errorRight);
		valueErrorMap[result.get()] = UDivExpr::create(resultError,
				result.get());
		currentError = valueErrorMap[result.get()];
		break;
	}
	case llvm::Instruction::Sub: {
		ref<Expr> lError = valueErrorMap[arguments[0].get()];
		ref<Expr> rError = valueErrorMap[arguments[1].get()];
		if (!lError.get()) {
			lError = getError(executor, arguments[0].get());
		}
		if (!rError.get()) {
			rError = getError(executor, arguments[1].get());
		}
		ref<Expr> extendedLeft = ZExtExpr::create(lError,
				arguments[0]->getWidth());
		ref<Expr> extendedRight = ZExtExpr::create(rError,
				arguments[1]->getWidth());
		ref<Expr> errorLeft = MulExpr::create(extendedLeft.get(),
				arguments[0].get());
		ref<Expr> errorRight = MulExpr::create(extendedRight.get(),
				arguments[1].get());
		ref<Expr> resultError = AddExpr::create(errorLeft, errorRight);
		valueErrorMap[result.get()] = UDivExpr::create(resultError,
				result.get());
		currentError = valueErrorMap[result.get()];
		break;
	}
	case llvm::Instruction::Mul: {
		ref<Expr> lError = valueErrorMap[arguments[0].get()];
		ref<Expr> rError = valueErrorMap[arguments[1].get()];
		if (!lError.get()) {
			lError = getError(executor, arguments[0].get());
		}
		if (!rError.get()) {
			rError = getError(executor, arguments[1].get());
		}
		valueErrorMap[result.get()] = AddExpr::create(lError, rError);
		currentError = valueErrorMap[result.get()];
		break;
	}
	case llvm::Instruction::UDiv: {
		ref<Expr> lError = valueErrorMap[arguments[0].get()];
		ref<Expr> rError = valueErrorMap[arguments[1].get()];
		if (!lError.get()) {
			lError = getError(executor, arguments[0].get());
		}
		if (!rError.get()) {
			rError = getError(executor, arguments[1].get());
		}
		valueErrorMap[result.get()] = AddExpr::create(lError, rError);
		currentError = valueErrorMap[result.get()];
		break;
	}
	case llvm::Instruction::SDiv: {
		ref<Expr> lError = valueErrorMap[arguments[0].get()];
		ref<Expr> rError = valueErrorMap[arguments[1].get()];
		if (!lError.get()) {
			lError = getError(executor, arguments[0].get());
		}
		if (!rError.get()) {
			rError = getError(executor, arguments[1].get());
		}
		valueErrorMap[result.get()] = AddExpr::create(lError, rError);
		currentError = valueErrorMap[result.get()];
		break;
	}
	}
}
