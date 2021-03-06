//===-- Z3ErrorSolver.cpp --------------------------------------*- C++ -*-====//
//
// The KLEE Symbolic Virtual Machine with Numerical Error Analysis Extension
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Config/config.h"
#include "klee/Internal/Support/ErrorHandling.h"
#ifdef ENABLE_Z3
#include "Z3ErrorBuilder.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprUtil.h"

#include "llvm/Support/ErrorHandling.h"

namespace klee {

class Z3ErrorSolverImpl : public SolverImpl {
private:
  Z3ErrorBuilder *errorBoundBuilder;
  Z3ErrorBuilder *pathConditionBuilder;
  double timeout;
  SolverRunStatus runStatusCode;
  ::Z3_params errorBoundSolverParameter;
  ::Z3_params pathConditionSolverParameter;
  // Parameter symbols
  ::Z3_symbol timeoutParamStrSymbol;

  bool internalRunSolver(const Query &,
                         const std::vector<const Array *> *objects,
                         std::vector<std::vector<unsigned char> > *values,
                         bool &hasSolution);

  bool internalRunOptimize(const Query &,
                           const std::vector<const Array *> *objects,
                           std::vector<bool> *infinity,
                           std::vector<std::pair<int, double> > *values,
                           std::vector<bool> *epsilon, bool &hasSolution);

public:
  Z3ErrorSolverImpl();
  ~Z3ErrorSolverImpl();

  char *getConstraintLog(const Query &);
  void setCoreSolverTimeout(double _timeout) {
    assert(_timeout >= 0.0 && "timeout must be >= 0");
    timeout = _timeout;

    unsigned int timeoutInMilliSeconds = (unsigned int)((timeout * 1000) + 0.5);
    if (timeoutInMilliSeconds == 0)
      timeoutInMilliSeconds = UINT_MAX;
    Z3_params_set_uint(errorBoundBuilder->ctx, errorBoundSolverParameter,
                       timeoutParamStrSymbol, timeoutInMilliSeconds);
    Z3_params_set_uint(pathConditionBuilder->ctx, pathConditionSolverParameter,
                       timeoutParamStrSymbol, timeoutInMilliSeconds);
  }

  bool computeTruth(const Query &, bool &isValid);
  bool computeValue(const Query &, ref<Expr> &result);
  bool computeInitialValues(const Query &,
                            const std::vector<const Array *> &objects,
                            std::vector<std::vector<unsigned char> > &values,
                            bool &hasSolution);
  bool computeOptimalValues(const Query &,
                            const std::vector<const Array *> &objects,
                            std::vector<bool> &infinity,
                            std::vector<std::pair<int, double> > &values,
                            std::vector<bool> &epsilon, bool &hasSolution);
  SolverRunStatus
  handleSolverResponse(::Z3_solver theSolver, ::Z3_lbool satisfiable,
                       const std::vector<const Array *> *objects,
                       std::vector<std::vector<unsigned char> > *values,
                       bool &hasSolution);
  SolverRunStatus
  handleOptimizeResponse(::Z3_optimize theSolver, ::Z3_lbool satisfiable,
                         const std::vector<const Array *> *objects,
                         std::vector<bool> *infinity,
                         std::vector<std::pair<int, double> > *values,
                         std::vector<bool> *epsilon, bool &hasSolution);
  SolverRunStatus getOperationStatusCode();
};

Z3ErrorSolverImpl::Z3ErrorSolverImpl()
    : errorBoundBuilder(new Z3ErrorBuilder(ComputeErrorBound == VIA_INTEGER,
                                           /*autoClearConstructCache=*/false)),
      pathConditionBuilder(
          new Z3ErrorBuilder(false, /*autoClearConstructCache=*/false)),
      timeout(0.0), runStatusCode(SOLVER_RUN_STATUS_FAILURE) {
  assert(errorBoundBuilder && "unable to create Z3Builder");
  errorBoundSolverParameter = Z3_mk_params(errorBoundBuilder->ctx);
  pathConditionSolverParameter = Z3_mk_params(pathConditionBuilder->ctx);
  Z3_params_inc_ref(errorBoundBuilder->ctx, errorBoundSolverParameter);
  Z3_params_inc_ref(errorBoundBuilder->ctx, pathConditionSolverParameter);
  timeoutParamStrSymbol =
      Z3_mk_string_symbol(errorBoundBuilder->ctx, "timeout");
  setCoreSolverTimeout(timeout);

  if (!UniformInputError) {
    // Set pareto optimality as priority strategy
    ::Z3_symbol priorityParamStrSymbol =
        Z3_mk_string_symbol(errorBoundBuilder->ctx, "priority");
    ::Z3_symbol pareto = Z3_mk_string_symbol(errorBoundBuilder->ctx, "pareto");
    Z3_params_set_symbol(errorBoundBuilder->ctx, errorBoundSolverParameter,
                         priorityParamStrSymbol, pareto);
  }
}

Z3ErrorSolverImpl::~Z3ErrorSolverImpl() {
  Z3_params_dec_ref(errorBoundBuilder->ctx, errorBoundSolverParameter);
  Z3_params_dec_ref(pathConditionBuilder->ctx, pathConditionSolverParameter);
  delete errorBoundBuilder;
}

Z3ErrorSolver::Z3ErrorSolver() : Solver(new Z3ErrorSolverImpl()) {}

char *Z3ErrorSolver::getConstraintLog(const Query &query) {
  return impl->getConstraintLog(query);
}

void Z3ErrorSolver::setCoreSolverTimeout(double timeout) {
  impl->setCoreSolverTimeout(timeout);
}

bool Z3ErrorSolver::computeOptimalValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<bool> &infinity, std::vector<std::pair<int, double> > &values,
    std::vector<bool> &epsilon, bool &hasSolution) {
  Z3ErrorSolverImpl *solverImpl = (Z3ErrorSolverImpl *)impl;
  return solverImpl->computeOptimalValues(query, objects, infinity, values,
                                          epsilon, hasSolution);
}

char *Z3ErrorSolverImpl::getConstraintLog(const Query &query) {
  std::vector<Z3ErrorASTHandle> assumptions;
  for (std::vector<ref<Expr> >::const_iterator it = query.constraints.begin(),
                                               ie = query.constraints.end();
       it != ie; ++it) {
    assumptions.push_back(errorBoundBuilder->construct(*it));
  }
  ::Z3_ast *assumptionsArray = NULL;
  int numAssumptions = query.constraints.size();
  if (numAssumptions) {
    assumptionsArray = (::Z3_ast *)malloc(sizeof(::Z3_ast) * numAssumptions);
    for (int index = 0; index < numAssumptions; ++index) {
      assumptionsArray[index] = (::Z3_ast)assumptions[index];
    }
  }

  // KLEE Queries are validity queries i.e.
  // ∀ X Constraints(X) → query(X)
  // but Z3 works in terms of satisfiability so instead we ask the
  // the negation of the equivalent i.e.
  // ∃ X Constraints(X) ∧ ¬ query(X)
  Z3ErrorASTHandle formula =
      Z3ErrorASTHandle(Z3_mk_not(errorBoundBuilder->ctx,
                                 errorBoundBuilder->construct(query.expr)),
                       errorBoundBuilder->ctx);

  ::Z3_string result = Z3_benchmark_to_smtlib_string(
      errorBoundBuilder->ctx,
      /*name=*/"Emited by klee::Z3ErrorSolverImpl::getConstraintLog()",
      /*logic=*/"",
      /*status=*/"unknown",
      /*attributes=*/"",
      /*num_assumptions=*/numAssumptions,
      /*assumptions=*/assumptionsArray,
      /*formula=*/formula);

  if (numAssumptions)
    free(assumptionsArray);
  // Client is responsible for freeing the returned C-string
  return strdup(result);
}

bool Z3ErrorSolverImpl::computeTruth(const Query &query, bool &isValid) {
  bool hasSolution;
  bool status =
      internalRunSolver(query, /*objects=*/NULL, /*values=*/NULL, hasSolution);
  isValid = !hasSolution;
  return status;
}

bool Z3ErrorSolverImpl::computeValue(const Query &query, ref<Expr> &result) {
  std::vector<const Array *> objects;
  std::vector<std::vector<unsigned char> > values;
  bool hasSolution;

  // Find the object used in the expression, and compute an assignment
  // for them.
  findSymbolicObjects(query.expr, objects);
  if (!computeInitialValues(query.withFalse(), objects, values, hasSolution))
    return false;
  assert(hasSolution && "state has invalid constraint set");

  // Evaluate the expression with the computed assignment.
  Assignment a(objects, values);
  result = a.evaluate(query.expr);

  return true;
}

bool Z3ErrorSolverImpl::computeInitialValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<std::vector<unsigned char> > &values, bool &hasSolution) {
  return internalRunSolver(query, &objects, &values, hasSolution);
}

bool Z3ErrorSolverImpl::computeOptimalValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<bool> &infinity, std::vector<std::pair<int, double> > &values,
    std::vector<bool> &epsilon, bool &hasSolution) {
  return internalRunOptimize(query, &objects, &infinity, &values, &epsilon,
                             hasSolution);
}

bool Z3ErrorSolverImpl::internalRunSolver(
    const Query &query, const std::vector<const Array *> *objects,
    std::vector<std::vector<unsigned char> > *values, bool &hasSolution) {
  TimerStatIncrementer t(stats::queryTime);
  // TODO: Does making a new solver for each query have a performance
  // impact vs making one global solver and using push and pop?
  // TODO: is the "simple_solver" the right solver to use for
  // best performance?
  Z3_solver theSolver = Z3_mk_simple_solver(pathConditionBuilder->ctx);
  Z3_solver_inc_ref(pathConditionBuilder->ctx, theSolver);
  Z3_solver_set_params(pathConditionBuilder->ctx, theSolver,
                       pathConditionSolverParameter);

  runStatusCode = SOLVER_RUN_STATUS_FAILURE;

  for (ConstraintManager::const_iterator it = query.constraints.begin(),
                                         ie = query.constraints.end();
       it != ie; ++it) {
    Z3_solver_assert(pathConditionBuilder->ctx, theSolver,
                     pathConditionBuilder->construct(*it));
  }
  ++stats::queries;
  if (objects)
    ++stats::queryCounterexamples;

  Z3ErrorASTHandle z3QueryExpr = Z3ErrorASTHandle(
      pathConditionBuilder->construct(query.expr), pathConditionBuilder->ctx);

  // KLEE Queries are validity queries i.e.
  // ∀ X Constraints(X) → query(X)
  // but Z3 works in terms of satisfiability so instead we ask the
  // negation of the equivalent i.e.
  // ∃ X Constraints(X) ∧ ¬ query(X)
  Z3_solver_assert(
      pathConditionBuilder->ctx, theSolver,
      Z3ErrorASTHandle(Z3_mk_not(pathConditionBuilder->ctx, z3QueryExpr),
                       pathConditionBuilder->ctx));

  ::Z3_lbool satisfiable =
      Z3_solver_check(pathConditionBuilder->ctx, theSolver);
  runStatusCode = handleSolverResponse(theSolver, satisfiable, objects, values,
                                       hasSolution);

  Z3_solver_dec_ref(pathConditionBuilder->ctx, theSolver);
  // Clear the builder's cache to prevent memory usage exploding.
  // By using ``autoClearConstructCache=false`` and clearning now
  // we allow Z3_ast expressions to be shared from an entire
  // ``Query`` rather than only sharing within a single call to
  // ``builder->construct()``.
  pathConditionBuilder->clearConstructCache();

  if (runStatusCode == SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE ||
      runStatusCode == SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE) {
    if (hasSolution) {
      ++stats::queriesInvalid;
    } else {
      ++stats::queriesValid;
    }
    return true; // success
  }
  return false; // failed
}

bool Z3ErrorSolverImpl::internalRunOptimize(
    const Query &query, const std::vector<const Array *> *objects,
    std::vector<bool> *infinity, std::vector<std::pair<int, double> > *values,
    std::vector<bool> *epsilon, bool &hasSolution) {
  TimerStatIncrementer t(stats::queryTime);
  // TODO: Does making a new solver for each query have a performance
  // impact vs making one global solver and using push and pop?
  // TODO: is the "simple_solver" the right solver to use for
  // best performance?
  Z3_optimize theSolver = Z3_mk_optimize(errorBoundBuilder->ctx);
  Z3_optimize_inc_ref(errorBoundBuilder->ctx, theSolver);
  Z3_optimize_set_params(errorBoundBuilder->ctx, theSolver,
                         errorBoundSolverParameter);

  runStatusCode = SOLVER_RUN_STATUS_FAILURE;

  for (ConstraintManager::const_iterator it = query.constraints.begin(),
                                         ie = query.constraints.end();
       it != ie; ++it) {
    Z3_optimize_assert(errorBoundBuilder->ctx, theSolver,
                       errorBoundBuilder->construct(*it));
  }
  ++stats::queries;
  if (objects)
    ++stats::queryCounterexamples;

  // Objective functions here
  for (std::vector<const Array *>::const_iterator it = objects->begin(),
                                                  ie = objects->end();
       it != ie; ++it) {
    const Array *array = *it;

    switch (ComputeErrorBound) {
    case VIA_INTEGER: {
      Z3ErrorASTHandle initial_read =
          errorBoundBuilder->buildInteger(array->name.c_str());
      Z3_optimize_maximize(errorBoundBuilder->ctx, theSolver, initial_read);
      break;
    }
    case VIA_REAL: {
      Z3ErrorASTHandle initial_read =
          errorBoundBuilder->buildReal(array->name.c_str());
      Z3_optimize_maximize(errorBoundBuilder->ctx, theSolver, initial_read);
      break;
    }
    default:
      break;
    }
  }

  if (DebugPrecision) {
    llvm::errs() << "Solving:\n";
    llvm::errs() << Z3_optimize_to_string(errorBoundBuilder->ctx, theSolver);
    llvm::errs() << "\n";
  }

  ::Z3_lbool satisfiable = Z3_optimize_check(errorBoundBuilder->ctx, theSolver);
  runStatusCode = handleOptimizeResponse(
      theSolver, satisfiable, objects, infinity, values, epsilon, hasSolution);

  Z3_optimize_dec_ref(errorBoundBuilder->ctx, theSolver);
  // Clear the builder's cache to prevent memory usage exploding.
  // By using ``autoClearConstructCache=false`` and clearning now
  // we allow Z3_ast expressions to be shared from an entire
  // ``Query`` rather than only sharing within a single call to
  // ``builder->construct()``.
  errorBoundBuilder->clearConstructCache();

  if (runStatusCode == SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE ||
      runStatusCode == SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE) {
    if (hasSolution) {
      ++stats::queriesInvalid;
    } else {
      ++stats::queriesValid;
    }
    return true; // success
  }
  return false; // failed
}

SolverImpl::SolverRunStatus Z3ErrorSolverImpl::handleSolverResponse(
    ::Z3_solver theSolver, ::Z3_lbool satisfiable,
    const std::vector<const Array *> *objects,
    std::vector<std::vector<unsigned char> > *values, bool &hasSolution) {
  switch (satisfiable) {
  case Z3_L_TRUE: {
    hasSolution = true;
    if (!objects) {
      // No assignment is needed
      assert(values == NULL);
      return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
    }
    assert(values && "values cannot be nullptr");
    ::Z3_model theModel =
        Z3_solver_get_model(pathConditionBuilder->ctx, theSolver);
    assert(theModel && "Failed to retrieve model");
    Z3_model_inc_ref(pathConditionBuilder->ctx, theModel);

    values->reserve(objects->size());
    for (std::vector<const Array *>::const_iterator it = objects->begin(),
                                                    ie = objects->end();
         it != ie; ++it) {
      const Array *array = *it;
      std::vector<unsigned char> data;

      data.reserve(8);

      unsigned num_constants =
          Z3_model_get_num_consts(pathConditionBuilder->ctx, theModel);
      for (unsigned i = 0; i < num_constants; i++) {
        Z3_symbol name;
        Z3_func_decl cnst =
            Z3_model_get_const_decl(pathConditionBuilder->ctx, theModel, i);
        Z3_ast a, v;
        Z3_bool ok;
        name = Z3_get_decl_name(pathConditionBuilder->ctx, cnst);
        if (Z3_get_symbol_kind(pathConditionBuilder->ctx, name) ==
            Z3_STRING_SYMBOL) {
          if (std::string(array->name) ==
              std::string(
                  Z3_get_symbol_string(pathConditionBuilder->ctx, name))) {
            a = Z3_mk_app(pathConditionBuilder->ctx, cnst, 0, 0);
            v = a;
            ok = Z3_model_eval(pathConditionBuilder->ctx, theModel, a,
                               /*model_completion=*/Z3_TRUE, &v);

            if (!ok) {
              assert(!"Failed to evaluate model");
            }

            int arrayElementValue = 0;
            bool successGet = Z3_get_numeral_int(pathConditionBuilder->ctx, v,
                                                 &arrayElementValue);
            if (successGet) {
              double doubleValue = (double)arrayElementValue;
              for (int i = 0; i < 8; ++i) {
                data.push_back(((unsigned char *)(&doubleValue))[i]);
              }
            } else {
              int numerator, denominator;
              bool successNumerator = Z3_get_numeral_int(
                  pathConditionBuilder->ctx,
                  Z3_get_numerator(pathConditionBuilder->ctx, v), &numerator);
              bool successDenominator = Z3_get_numeral_int(
                  pathConditionBuilder->ctx,
                  Z3_get_denominator(pathConditionBuilder->ctx, v),
                  &denominator);

              if (!(successNumerator && successDenominator))
                assert(!"failed to get value back");

              double result = ((double)numerator) / ((double)denominator);

              uint64_t intResult = 0;
              memcpy(&intResult, &result, 8);

              for (int i = 0; i < 8; ++i) {
                data.push_back(intResult & 255);
                intResult = intResult >> 8;
              }
            }

            values->push_back(data);

            break;
          }
        }
      }
    }

    Z3_model_dec_ref(pathConditionBuilder->ctx, theModel);
    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
  }
  case Z3_L_FALSE:
    hasSolution = false;
    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
  case Z3_L_UNDEF: {
    ::Z3_string reason =
        ::Z3_solver_get_reason_unknown(pathConditionBuilder->ctx, theSolver);
    if (strcmp(reason, "timeout") == 0 || strcmp(reason, "canceled") == 0) {
      return SolverImpl::SOLVER_RUN_STATUS_TIMEOUT;
    }
    if (strcmp(reason, "unknown") == 0) {
      return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
    }
    klee_warning("Unexpected solver failure. Reason is \"%s,\"\n", reason);
    abort();
  }
  default:
    llvm_unreachable("unhandled Z3 result");
  }
}

SolverImpl::SolverRunStatus Z3ErrorSolverImpl::handleOptimizeResponse(
    ::Z3_optimize theSolver, ::Z3_lbool satisfiable,
    const std::vector<const Array *> *objects, std::vector<bool> *infinity,
    std::vector<std::pair<int, double> > *values, std::vector<bool> *epsilon,
    bool &hasSolution) {
  switch (satisfiable) {
  case Z3_L_TRUE: {
    hasSolution = true;
    if (!objects) {
      // No assignment is needed
      assert(values == NULL);
      return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
    }
    assert(values && "values cannot be nullptr");
    values->reserve(objects->size());

    for (unsigned idx = 0; idx < objects->size(); ++idx) {
      std::vector<unsigned char> data;

      ::Z3_ast_vector upperBoundVector = Z3_optimize_get_upper_as_vector(
          errorBoundBuilder->ctx, theSolver, idx);

      Z3_ast_vector_inc_ref(errorBoundBuilder->ctx, upperBoundVector);

      ::Z3_ast infinityCoefficient =
          Z3_ast_vector_get(errorBoundBuilder->ctx, upperBoundVector, 0);
      ::Z3_ast upperBound =
          Z3_ast_vector_get(errorBoundBuilder->ctx, upperBoundVector, 1);
      ::Z3_ast epsilonCoefficient =
          Z3_ast_vector_get(errorBoundBuilder->ctx, upperBoundVector, 2);

      if (DebugPrecision) {
        llvm::errs()
            << "(infinity_coefficient, upper_bound, epsilon_coefficient) = ";
        llvm::errs() << "(" << Z3_ast_to_string(errorBoundBuilder->ctx,
                                                infinityCoefficient) << ",";
        llvm::errs() << Z3_ast_to_string(errorBoundBuilder->ctx, upperBound)
                     << ",";
        llvm::errs() << Z3_ast_to_string(errorBoundBuilder->ctx,
                                         epsilonCoefficient) << ")\n";
      }

      int infinity = 0;
      int epsilon = 0;
      bool successGet = Z3_get_numeral_int(errorBoundBuilder->ctx,
                                           infinityCoefficient, &infinity);

      if (successGet && infinity) {
        values->push_back(std::pair<int, double>(1, 0));
        continue;
      }

      successGet = Z3_get_numeral_int(errorBoundBuilder->ctx,
                                      epsilonCoefficient, &epsilon);

      if (successGet && epsilon) {
        values->push_back(std::pair<int, double>(2, 0));
        continue;
      }

      int upperBoundValue = 0;
      double result;

      successGet = Z3_get_numeral_int(errorBoundBuilder->ctx, upperBound,
                                      &upperBoundValue);
      if (successGet) {
        result = upperBoundValue;
      } else {
        int numerator, denominator;
        bool successNumerator = Z3_get_numeral_int(
            errorBoundBuilder->ctx,
            Z3_get_numerator(errorBoundBuilder->ctx, upperBound), &numerator);
        bool successDenominator = Z3_get_numeral_int(
            errorBoundBuilder->ctx,
            Z3_get_denominator(errorBoundBuilder->ctx, upperBound),
            &denominator);

        if (!(successNumerator && successDenominator))
          assert(!"failed to get value back");

        result = ((double)numerator) / ((double)denominator);
      }
      Z3_dec_ref(errorBoundBuilder->ctx, upperBound);

      values->push_back(std::pair<int, double>(0, result));
    }

    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
  }
  case Z3_L_FALSE:
    hasSolution = false;
    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
  case Z3_L_UNDEF: {
    ::Z3_string reason =
        ::Z3_optimize_get_reason_unknown(errorBoundBuilder->ctx, theSolver);
    if (strcmp(reason, "timeout") == 0 || strcmp(reason, "canceled") == 0) {
      return SolverImpl::SOLVER_RUN_STATUS_TIMEOUT;
    }
    if (strcmp(reason, "unknown") == 0) {
      return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
    }
    klee_warning("Unexpected solver failure. Reason is \"%s,\"\n", reason);
    abort();
  }
  default:
    llvm_unreachable("unhandled Z3 result");
  }
}

SolverImpl::SolverRunStatus Z3ErrorSolverImpl::getOperationStatusCode() {
  return runStatusCode;
}
}
#endif // ENABLE_Z3
