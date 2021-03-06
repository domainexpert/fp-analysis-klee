//===----- ErrorState.h ---------------------------------------------------===//
//
// The KLEE Symbolic Virtual Machine with Numerical Error Analysis Extension
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_ERRORSTATE_H_
#define KLEE_ERRORSTATE_H_

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/util/ArrayCache.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Instructions.h"
#else
#include "llvm/Instructions.h"
#endif

namespace klee {
class Executor;

class ErrorState {
public:
  unsigned refCount;

private:
  std::map<const Array *, const Array *> arrayErrorArrayMap;

  ref<Expr> getError(Executor *executor, ref<Expr> valueExpr,
                     llvm::Value *value = 0);

  ArrayCache *errorArrayCache;

  std::string outputString;

  std::map<uintptr_t, ref<Expr> > declaredInputError;

  std::map<uintptr_t, std::pair<ref<Expr>, ref<Expr> > > storedError;

  std::map<std::string, std::pair<std::string, ref<Expr> > > errorExpressions;

  std::vector<ref<Expr> > inputErrorList;

  std::map<std::string, std::vector<Cell> > mathCallArgs;

  std::vector<std::pair<unsigned, std::string> > memcpyStoreInfo;

  //@breif Used to generate return variable names for math function calls
  int mathVarCount;

public:
  ErrorState(ArrayCache *arrayCache)
      : refCount(0), errorArrayCache(arrayCache), mathVarCount(0) {}

  ErrorState(ErrorState &errorState)
      : refCount(0), errorArrayCache(errorState.errorArrayCache) {
    declaredInputError = errorState.declaredInputError;
    storedError = errorState.storedError;
    errorExpressions = errorState.errorExpressions;
    inputErrorList = errorState.inputErrorList;
    outputString = errorState.outputString;
    mathCallArgs = errorState.mathCallArgs;
    mathVarCount = errorState.mathVarCount;
    memcpyStoreInfo = errorState.memcpyStoreInfo;
  }

  ~ErrorState();

  void outputComputedErrorBound(
      std::vector<std::pair<int, double> > doublePrecision);

  ConstraintManager outputErrorBound(llvm::Instruction *inst, ref<Expr> error,
                                     double bound, std::string name,
                                     std::vector<ref<Expr> > &_inputErrorList);

  std::pair<ref<Expr>, ref<Expr> > propagateError(Executor *executor,
                                                  llvm::Instruction *instr,
                                                  ref<Expr> result,
                                                  std::vector<Cell> &arguments);

  std::string &getOutputString() { return outputString; }

  void registerInputError(ref<Expr> error);

  void executeStoreSimple(ref<Expr> base, ref<Expr> address, ref<Expr> value,
                          ref<Expr> error, ref<Expr> valueWithError,
                          llvm::Instruction *inst);

  void declareInputError(ref<Expr> address, ref<Expr> error);

  std::pair<ref<Expr>, ref<Expr> > retrieveStoredError(ref<Expr> address) const;

  ref<Expr> retrieveDeclaredInputError(ref<Expr> address) const;

  bool hasStoredError(ref<Expr> address) const;

  bool hasDeclaredInputError(ref<Expr> address) const;

  /// \brief Retrieve the error expression from the stored error expressions
  /// map. This returns 0 when the address is not found.
  std::pair<ref<Expr>, ref<Expr> > executeLoad(llvm::Instruction *inst,
                                               ref<Expr> base,
                                               ref<Expr> address,
                                               ref<Expr> offset);

  /// print - Print the object content to stream
  void print(llvm::raw_ostream &os) const;

  // Add to constraint lists the constraint scalingVar != 0
  ref<Expr> getScalingConstraint();

  // Getter for error expressions
  std::map<std::string, std::pair<std::string, ref<Expr> > > &
  getStateErrorExpressions();

  // Getter for math call functions and arguments
  std::map<std::string, std::vector<Cell> > &getMathExpressions();

  /// dump - Print the object content to stderr
  void dump() const {
    print(llvm::errs());
    llvm::errs() << "\n";
  }

  ref<Expr> createNewMathErrorVar(ref<Expr> mathVar, std::string mathVarName);

  void storeMathCallArgs(std::string varName, std::vector<Cell> &arguments);

  std::string createNewMathVarName(std::string mathFunctionName);

  void saveMemcpyStoreInfo(llvm::Instruction *inst, unsigned lineNumber,
                           std::string functionName);

  std::pair<unsigned, std::string> retrieveMemcpyStoreInfo();
};
}

#endif /* KLEE_ERRORSTATE_H_ */
