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

#include "klee/Expr.h"
#include "klee/util/ArrayCache.h"
#include "klee/Internal/Module/Cell.h"

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

  ArrayCache errorArrayCache;

  std::string outputString;

  std::map<uintptr_t, ref<Expr> > storedError;

  std::vector<ref<Expr> > inputErrorList;

public:
  ErrorState() : refCount(0) {}

  ErrorState(ErrorState &symErr) : refCount(0) {
    storedError = symErr.storedError;
    // FIXME: Simple copy for now.
    inputErrorList = symErr.inputErrorList;
  }

  ~ErrorState();

  void outputErrorBound(llvm::Instruction *inst, ref<Expr> error, double bound);

  ref<Expr> propagateError(Executor *executor, llvm::Instruction *instr,
                           ref<Expr> result, std::vector<Cell> &arguments);

  std::string &getOutputString() { return outputString; }

  void registerInputError(ref<Expr> error) { inputErrorList.push_back(error); }

  void executeStoreSimple(llvm::Instruction *inst, ref<Expr> address,
                          ref<Expr> error);

  ref<Expr> retrieveStoredError(ref<Expr> address) const;

  bool hasStoredError(ref<Expr> address) const;

  ref<Expr> executeLoad(llvm::Value *value, ref<Expr> address);

  /// \brief Overwrite the contents of the current error state with another
  void overwriteWith(ref<ErrorState> overwriting);

  /// print - Print the object content to stream
  void print(llvm::raw_ostream &os) const;

  /// dump - Print the object content to stderr
  void dump() const {
    print(llvm::errs());
    llvm::errs() << "\n";
  }
};
}

#endif /* KLEE_ERRORSTATE_H_ */
