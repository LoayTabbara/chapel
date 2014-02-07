//
// Transformations for begin, cobegin, and on statements
//

#include "astutil.h"
#include "expr.h"
#include "optimizations.h"
#include "passes.h"
#include "../resolution/resolution.h"
#include "stmt.h"
#include "symbol.h"
#include "stringutil.h"
#include "driver.h"
#include "files.h"
#include "stlUtil.h"

// Notes on
//   makeHeapAllocations()    //invoked from parallel()
//   insertWideReferences()
//
//------
// Terminology/abbreviations:
//
// 'on'+'begin' is considered to be both an 'on' and a 'begin'
// - see also FLAG_NON_BLOCKING
//
// A "global" is a module-level symbol, usu. a VarSymbol.
// A 'var' is a global if, equivalently:
//  isModuleSymbol(var->defPoint->parentSymbol)
//  isGlobal(var)
//
// MHA = makeHeapAllocations() and functions it invokes
// IWR = insertWideReferences() and functions it invokes
//------
//
// MHA and IWR take care of the following, among others:
// - heap allocation for remote access
// - heap allocation for 'begin'
// - change acces to variable -> access to its ._value
// - set up wide references
// - broadcasting of globals
//
// In more details:
//
// Heap allocation for remote access is done:
// - for globals - in IWR, if:
//    requireWideReferences()
// - for a local - in MHA, if:
//    needHeapVars() && the local can be passed to an 'on'
//
// Heap allocation for 'begin' is done:
// - for globals - n/a
//    see above instead
// - for a local - in MHA, if:
//    the local can be passed to a 'begin'
//
// Change acces to variable -> access to its ._value
// - for globals - in MHA, if:
//    requireWideReferences()
// - for locals - in MHA, if:
//    the local is subject to heap allocation
//    in either of the above two categories
//
// Wide references are set up in IWR, if:
//    requireWideReferences()
//
// Broadcasting:
// - of certain global constants       - in MHA/findHeapVarsAndRefs, if:
//    !fLocal
// - of global arrays/domains/distribs - in MHA/findHeapVarsAndRefs, if:
//    !fLocal
// - of locations of the other globals - in IWR, if:
//    requireWideReferences()


typedef struct {
  bool firstCall;
  ClassType* ctype;
  FnSymbol*  wrap_fn;
} BundleArgsFnData;

// bundleArgsFnDataInit: the initial value for BundleArgsFnData
static BundleArgsFnData bundleArgsFnDataInit = { true, NULL, NULL };

static void insertEndCounts();
static void passArgsToNestedFns(Vec<FnSymbol*>& nestedFunctions);
static void create_block_fn_wrapper(FnSymbol* fn, CallExpr* fcall, BundleArgsFnData &baData);
static void call_block_fn_wrapper(FnSymbol* fn, CallExpr* fcall, VarSymbol* tempc, FnSymbol *wrap_fn);
static void findBlockRefActuals(Vec<Symbol*>& refSet, Vec<Symbol*>& refVec);
static void findHeapVarsAndRefs(Map<Symbol*,Vec<SymExpr*>*>& defMap,
                                Vec<Symbol*>& refSet, Vec<Symbol*>& refVec,
                                Vec<Symbol*>& varSet, Vec<Symbol*>& varVec);
static void convertNilToObject();
static void buildWideClasses();
static void widenClasses();
static void buildWideRefMap();
static void widenRefs();
static void insertElementAccessTemps();
static void narrowWideClassesThroughCalls();
static void insertWideClassTempsForNil();
static void insertWideCastTemps();
static void derefWideStringActuals();
static void derefWideRefsToWideClasses();
static void widenGetPrivClass();
static void moveAddressSourcesToTemp(void);

// Package args into a class and call a wrapper function with that
// object. The wrapper function will then call the function
// created by the previous parallel pass. This is a way to pass along
// multiple args through the limitation of one arg in the runtime's
// thread creation interface. 
//
// Implemented using BundleArgsFnData and the functions:
//   create_arg_bundle_class
//   bundleArgs
//   create_block_fn_wrapper
//   call_block_fn_wrapper

// Even though the arg bundle class depends only on the iterator,
// current code unfortunately uses the call site for some information
// If there are multiple call sites, the first one is used.

static void create_arg_bundle_class(FnSymbol* fn, CallExpr* fcall, ModuleSymbol* mod, BundleArgsFnData &baData) {
  INT_ASSERT(!baData.ctype);
  SET_LINENO(fn);

// Here, 'fcall' is the first of fn's callees and so it acts as a
// representative of all the other callees, if any.
// As of this writing, this should be OK because the callees are
// obtained by duplicating the original call, which resulted in
// outlining a block into 'fn' and so is unique.
// To eliminate 'fcall' in create_arg_bundle_class(), we need
// to rely on fn's formal types instead of fcall's actual types.

  // create a new class to capture refs to locals
  ClassType* ctype = new ClassType( CLASS_CLASS);
  TypeSymbol* new_c = new TypeSymbol(astr("_class_locals", fn->name), ctype);
  new_c->addFlag(FLAG_NO_OBJECT);
  new_c->addFlag(FLAG_NO_WIDE_CLASS);

  // add the function args as fields in the class
  int i = 0;    // Fields are numbered for uniqueness.
  for_actuals(arg, fcall) {
    SymExpr *s = toSymExpr(arg);
    Symbol  *var = s->var; // arg or var
    var->addFlag(FLAG_CONCURRENTLY_ACCESSED);
    VarSymbol* field = new VarSymbol(astr("_", istr(i), "_", var->name), var->type);
    ctype->fields.insertAtTail(new DefExpr(field));
    i++;
  }
  // BTW 'mod' may differ from fn->defPoint->getModule()
  // e.g. due to iterator inlining.
  mod->block->insertAtHead(new DefExpr(new_c));

  baData.ctype = ctype;
}


/// Optionally autoCopies an argument being inserted into an argument bundle.
///
/// This routine optionally inserts an autoCopy ahead of each invocation of a
/// task function that begins asynchronous execution (currently just "begin" and
/// "nonblocking on" functions).  
/// If such an autoCopy call is inserted, a matching autoDestroy call is placed
/// at the end of the tasking routine before the call to _downEndCount.  Since a
/// tasking function may be called from several call sites, the task function is
/// modified only when processing the first invocation.
/// The insertion of autoCopy calls is required for internally reference-counted
/// types, and also for all user-defined record types (passed by value).  For
/// internally reference-counted types, the autoCopy call increases the
/// reference count, so the internal (reference-counted) data is not reclaimed
/// before the task function exits.  For user-defined record types, the autoCopy
/// call provides a hook so the record author can ensure that the task function
/// owns its own copy of the record (including, but not limited to,
/// reference-counting it).
/// \param firstCall Should be set to \p true for the first invocation of a
/// given task function and \p false thereafter.
/// \ret Returns the result of calling autoCopy on the given arg, if necessary;
/// otherwise, just returns 
static Symbol* insertAutoCopyDestroyForTaskArg
  (Expr* arg, ///< The actual argument being passed.
   CallExpr* fcall, ///< The call that invokes the task function.
   FnSymbol* fn, ///< The task function.
   bool firstCall)
{
  SymExpr* s = toSymExpr(arg);
  Symbol* var = s->var;

  // This applies only to arguments being passed to asynchronous task functions.
  if (fn->hasFlag(FLAG_BEGIN) ||
      (fn->hasFlag(FLAG_ON) && fn->hasFlag(FLAG_NON_BLOCKING)))
  {
    Type* baseType = arg->getValType();
    FnSymbol* autoCopyFn = getAutoCopy(baseType);
    FnSymbol* autoDestroyFn = getAutoDestroy(baseType);

    if (isRefCountedType(baseType))
    {
      if (arg->typeInfo() != baseType)
      {
        // For internally reference-counted types, this punches through
        // references to bump the reference count.
        VarSymbol* derefTmp = newTemp(baseType);
        fcall->insertBefore(new DefExpr(derefTmp));
        fcall->insertBefore(new CallExpr(PRIM_MOVE, derefTmp,
                                         new CallExpr(PRIM_DEREF, var)));
        // The result of the autoCopy call is dropped on the floor.
        // It is only called to increment the ref count.
        fcall->insertBefore(new CallExpr(autoCopyFn, derefTmp));
        // But the original var is passed through to the field assignment.
      }
      else
      {
        VarSymbol* valTmp = newTemp(baseType);
        valTmp->addFlag(FLAG_NECESSARY_AUTO_COPY);
        fcall->insertBefore(new DefExpr(valTmp));
        fcall->insertBefore(new CallExpr(PRIM_MOVE, valTmp,
                                         new CallExpr(autoCopyFn, var)));
        // If the arg is not passed by reference, the result of the autoCopy is
        // passed to the field assignment.
        var = valTmp;
      }

      if (firstCall)
      {
        // The task function may be called from several call sites, so insert
        // the autodestroy call only once (when processing the first fcall).
        Symbol* formal = actual_to_formal(arg);
        if (arg->typeInfo() != baseType)
        {
          VarSymbol* derefTmp = newTemp(baseType);
          fn->insertBeforeDownEndCount(new DefExpr(derefTmp));
          fn->insertBeforeDownEndCount(
            new CallExpr(PRIM_MOVE, derefTmp, new CallExpr(PRIM_DEREF,  formal)));
          formal = derefTmp;
        }
        fn->insertBeforeDownEndCount(new CallExpr(autoDestroyFn, formal));
      }
    }
    else if (isRecord(baseType))
    {
      // Do this only if the record is passed by value.
      if (arg->typeInfo() == baseType)
      {
        // TODO: Find out why _RuntimeTypeInfo records do not have autoCopy
        // functions, so we can get rid of this special test.
        if (autoCopyFn == NULL) return var;

        // Insert a call to the autoCopy function ahead of the call.
        VarSymbol* valTmp = newTemp(baseType);
        fcall->insertBefore(new DefExpr(valTmp));
        CallExpr* autoCopyCall = new CallExpr(autoCopyFn, var);
        fcall->insertBefore(new CallExpr(PRIM_MOVE, valTmp, autoCopyCall));
        var = valTmp;

        if (firstCall)
        {
          // Insert a call to the autoDestroy function ahead of the return.
          // (But only once per function for each affected argument.)
          Symbol* formal = actual_to_formal(arg);
          CallExpr* autoDestroyCall = new CallExpr(autoDestroyFn,formal);
          fn->insertBeforeDownEndCount(autoDestroyCall);
        }
      }
    }
  }
  return var;
}


static void
bundleArgs(CallExpr* fcall, BundleArgsFnData &baData) {
  SET_LINENO(fcall);
  ModuleSymbol* mod = fcall->getModule();
  FnSymbol* fn = fcall->isResolved();

  const bool firstCall = baData.firstCall;
  if (firstCall)
    create_arg_bundle_class(fn, fcall, mod, baData);
  ClassType* ctype = baData.ctype;

  // create the class variable instance and allocate space for it
  VarSymbol *tempc = newTemp(astr("_args_for", fn->name), ctype);
  fcall->insertBefore( new DefExpr( tempc));
  insertChplHereAlloc(fcall, false /*insertAfter*/, tempc,
                      ctype, newMemDesc("bundled args"));

  // set the references in the class instance
  int i = 1;
  for_actuals(arg, fcall) 
  {
    // Insert autoCopy/autoDestroy as needed for "begin" or "nonblocking on"
    // calls.
    Symbol  *var = insertAutoCopyDestroyForTaskArg(arg, fcall, fn, firstCall);

    // Copy the argument into the corresponding slot in the argument bundle.
    CallExpr *setc = new CallExpr(PRIM_SET_MEMBER,
                                  tempc,
                                  ctype->getField(i),
                                  var);
    fcall->insertBefore(setc);
    i++;
  }

  // create wrapper-function that uses the class instance
  create_block_fn_wrapper(fn, fcall, baData);
  call_block_fn_wrapper(fn, fcall, tempc, baData.wrap_fn);
  baData.firstCall = false;
}


static void create_block_fn_wrapper(FnSymbol* fn, CallExpr* fcall, BundleArgsFnData &baData)
{
  ModuleSymbol* mod = fcall->getModule();
  INT_ASSERT(fn == fcall->isResolved());

  INT_ASSERT(baData.firstCall == !baData.wrap_fn);
  if (!baData.firstCall) return;

  ClassType* ctype = baData.ctype;
  FnSymbol *wrap_fn = new FnSymbol( astr("wrap", fn->name));

  // Add a special flag to the wrapper-function as appropriate.
  // These control aspects of code generation.
  if (fn->hasFlag(FLAG_ON))                     wrap_fn->addFlag(FLAG_ON_BLOCK);
  if (fn->hasFlag(FLAG_NON_BLOCKING))           wrap_fn->addFlag(FLAG_NON_BLOCKING);
  if (fn->hasFlag(FLAG_COBEGIN_OR_COFORALL))    wrap_fn->addFlag(FLAG_COBEGIN_OR_COFORALL_BLOCK);
  if (fn->hasFlag(FLAG_BEGIN))                  wrap_fn->addFlag(FLAG_BEGIN_BLOCK);

  if (fn->hasFlag(FLAG_ON)) {
    // The wrapper function for 'on' block has an additional argument, which
    // passes the new wide locale pointer to the fork function.
    // This argument is stripped from the wrapper function during code generation.
    // As far as the compiler knows, the call looks like:
    //  wrapon_fn(new_locale, wrapped_args)
    // and the wrapon_fn has a matching signature.  But at codegen time, this is
    // translated to:
    //  fork(new_locale.locale.node, wrapon_fn, wrapped_args)
    // The fork function effective generates the call
    //  wrapon_fn(wrapped_args)
    // (without the locale arg).

    // The locale arg is originally attached to the on_fn, but we copy it 
    // into the wrapper here, and then later on remove it completely.
    // The on_fn does not need this extra argument, and can find out its locale
    // by reading the task-private "here" pointer.
    DefExpr* localeArg = toDefExpr(fn->formals.get(1)->copy());
    // The above copy() used to be a remove(), based on the assumption that there was
    // exactly one wrapper for each on.  Now, the on_fn is outlined early and has
    // several callers, therefore severall wrapon_fns are generated.
    // So, we leave the extra locale arg in place here and remove it later 
    // (see the last if (fn->hasFlag(FLAG_ON)) clause in passArgsToNestedFns()).
    wrap_fn->insertFormalAtTail(localeArg);
  }

  ArgSymbol *wrap_c = new ArgSymbol( INTENT_CONST_REF, "c", ctype);
  wrap_fn->insertFormalAtTail(wrap_c);

  mod->block->insertAtTail(new DefExpr(wrap_fn));

  // Create a call to the original function
  CallExpr *call_orig = new CallExpr(fn);
  bool first = true;
  for_fields(field, ctype)
  {
    // insert args
    VarSymbol* tmp = newTemp(field->name, field->type);
    wrap_fn->insertAtTail(new DefExpr(tmp));
    wrap_fn->insertAtTail(
        new CallExpr(PRIM_MOVE, tmp,
        new CallExpr(PRIM_GET_MEMBER_VALUE, wrap_c, field)));

    // Special case: 
    // If this is an on block, remember the first field,
    // but don't add to the list of actuals passed to the original on_fn.
    // It contains the locale on which the new task is launched.
    if (first && fn->hasFlag(FLAG_ON))
      /* no-op */;
    else
      call_orig->insertAtTail(tmp);

    first = false;
  }

  wrap_fn->retType = dtVoid;
  wrap_fn->insertAtTail(call_orig);     // add new call

  if (fn->hasFlag(FLAG_ON))
    ; // the caller will free the actual
  else
    wrap_fn->insertAtTail(callChplHereFree(wrap_c));

  wrap_fn->insertAtTail(new CallExpr(PRIM_RETURN, gVoid));

  // 'fn' has already been flattened and hoisted to the top level.
  // We leave 'fn' in the module where it was placed originally,
  // whereas 'wrap_fn' is in fcall's module.
  // These two modules may be different, e.g. due to iterator inlining.
  INT_ASSERT(isGlobal(fn));

  baData.wrap_fn = wrap_fn;
}

static void call_block_fn_wrapper(FnSymbol* fn, CallExpr* fcall, VarSymbol* tempc, FnSymbol *wrap_fn)
{
  // The wrapper function is called with the bundled argument list.
  if (fn->hasFlag(FLAG_ON)) {
    // For an on block, the first argument is also passed directly
    // to the wrapper function.
    // The forking function uses this to fork a task on the target locale.
    fcall->insertBefore(new CallExpr(wrap_fn, fcall->get(1)->remove(), tempc));
  } else
    fcall->insertBefore(new CallExpr(wrap_fn, tempc));

  if (fn->hasFlag(FLAG_ON))
    fcall->insertAfter(callChplHereFree(tempc));
  else
    ; // wrap_fn will free the formal

  fcall->remove();                     // rm orig. call
}


static void
insertEndCount(FnSymbol* fn,
               Type* endCountType,
               Vec<FnSymbol*>& queue,
               Map<FnSymbol*,Symbol*>& endCountMap) {
  if (fn == chpl_gen_main) {
    VarSymbol* var = newTemp("_endCount", endCountType);
    fn->insertAtHead(new DefExpr(var));
    endCountMap.put(fn, var);
    queue.add(fn);
  } else {
    ArgSymbol* arg = new ArgSymbol(INTENT_CONST_REF, "_endCount", endCountType);
    fn->insertFormalAtTail(arg);
    VarSymbol* var = newTemp("_endCount", endCountType);
    fn->insertAtHead(new CallExpr(PRIM_MOVE, var, arg));
    fn->insertAtHead(new DefExpr(var));
    endCountMap.put(fn, var);
    queue.add(fn);
  }
}


static void
replicateGlobalRecordWrappedVars(DefExpr *def) {
  ModuleSymbol* mod = toModuleSymbol(def->parentSymbol);
  Expr* stmt = mod->initFn->body->body.head;
  Expr* useFirst = NULL;
  Symbol *currDefSym = def->sym;
  bool found = false;
  // Try to find the first definition of this variable in the
  //   module initialization function
  while (stmt->next && !found) {
    stmt = stmt->next;
    Vec<SymExpr*> symExprs;
    collectSymExprs(stmt, symExprs);
    forv_Vec(SymExpr, se, symExprs) {
      if (se->var == currDefSym) {
        INT_ASSERT(se->parentExpr);
        int result = isDefAndOrUse(se);
        if (result & 1) {
          // first use/def of the variable is a def (normal case)
          INT_ASSERT(useFirst==NULL);
          found = true;
          break;
        } else if (result & 2) {
          if (useFirst == NULL) {
            // This statement captures a reference to the variable
            // to pass it to the function that builds the initializing
            // expression
            CallExpr *parent = toCallExpr(se->parentExpr);
            INT_ASSERT(parent);
            INT_ASSERT(parent->isPrimitive(PRIM_ADDR_OF));
            INT_ASSERT(isCallExpr(parent->parentExpr));
            // Now start looking for the first use of the captured
            // reference
            currDefSym = toSymExpr(toCallExpr(parent->parentExpr)->get(1))->var;
            INT_ASSERT(currDefSym);
            // This is used to flag that we have found the first use
            // of the variable
            useFirst = stmt;
          } else {
            // This statement builds the initializing expression, so
            // we can insert the broadcast after this statement

            // These checks may need to change if we change the way
            // we handle domain literals, forall expressions, and/or
            // depending on how we add array literals to the language
            INT_ASSERT(toCallExpr(stmt));
            INT_ASSERT(toCallExpr(stmt)->primitive==NULL);
            found = true;
            break;
          }
        }
      }
    }
  }
  stmt->insertAfter(new CallExpr(PRIM_PRIVATE_BROADCAST, def->sym));
}


static ClassType*
buildHeapType(Type* type) {
  static Map<Type*,ClassType*> heapTypeMap;
  if (heapTypeMap.get(type))
    return heapTypeMap.get(type);

  SET_LINENO(type->symbol);
  ClassType* heap = new ClassType(CLASS_CLASS);
  TypeSymbol* ts = new TypeSymbol(astr("heap_", type->symbol->cname), heap);
  ts->addFlag(FLAG_NO_OBJECT);
  ts->addFlag(FLAG_HEAP);
  theProgram->block->insertAtTail(new DefExpr(ts));
  heap->fields.insertAtTail(new DefExpr(new VarSymbol("value", type)));
  heapTypeMap.put(type, heap);
  return heap;
}


static void
freeHeapAllocatedVars(Vec<Symbol*> heapAllocatedVars) {
  Vec<FnSymbol*> fnsContainingTaskll;

  // start with the functions created from begin, cobegin, and coforall statements
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_BEGIN) || fn->hasFlag(FLAG_COBEGIN_OR_COFORALL) ||
        fn->hasFlag(FLAG_NON_BLOCKING))
      fnsContainingTaskll.add(fn);
  }
  // add any functions that call the functions added so far
  forv_Vec(FnSymbol, fn, fnsContainingTaskll) {
    forv_Vec(CallExpr, call, *fn->calledBy) {
      if (call->parentSymbol) {
        FnSymbol* caller = toFnSymbol(call->parentSymbol);
        INT_ASSERT(caller);
        fnsContainingTaskll.add_exclusive(caller);
      }
    }
  }

  Vec<Symbol*> symSet;
  Vec<BaseAST*> asts;
  Vec<SymExpr*> symExprs;
  collect_asts(rootModule, asts);
  forv_Vec(BaseAST, ast, asts) {
    if (DefExpr* def = toDefExpr(ast)) {
      if (def->parentSymbol) {
        if (isVarSymbol(def->sym) || isArgSymbol(def->sym)) {
          symSet.set_add(def->sym);
        }
      }
    } else if (SymExpr* se = toSymExpr(ast)) {
      symExprs.add(se);
    }
  }
  Map<Symbol*,Vec<SymExpr*>*> defMap;
  Map<Symbol*,Vec<SymExpr*>*> useMap;
  buildDefUseMaps(symSet, symExprs, defMap, useMap);

  forv_Vec(Symbol, var, heapAllocatedVars) {
    // find out if a variable that was put on the heap could be passed in as an
    // argument to a function created from a begin, cobegin, or coforall statement;
    // if not, free the heap memory just allocated at the end of the block
    if (defMap.get(var)->n == 1) {
      bool freeVar = true;
      Vec<Symbol*> varsToTrack;
      varsToTrack.add(var);
      forv_Vec(Symbol, v, varsToTrack) {
        if (useMap.get(v)) {
          forv_Vec(SymExpr, se, *useMap.get(v)) {
            if (CallExpr* call = toCallExpr(se->parentExpr)) {
              if (call->isPrimitive(PRIM_ADDR_OF) ||
                  call->isPrimitive(PRIM_GET_MEMBER) ||
                  call->isPrimitive(PRIM_GET_SVEC_MEMBER) ||
                  call->isPrimitive(PRIM_WIDE_GET_LOCALE) ||
                  call->isPrimitive(PRIM_WIDE_GET_NODE))
                // Treat the use of these primitives as a use of their arguments.
                call = toCallExpr(call->parentExpr);
              if (call->isPrimitive(PRIM_MOVE) || call->isPrimitive(PRIM_ASSIGN))
                varsToTrack.add(toSymExpr(call->get(1))->var);
              else if (fnsContainingTaskll.in(call->isResolved())) {
                freeVar = false;
                break;
              }
            }
          }
          if (!freeVar) break;
        }
      }
      if (freeVar) {
        CallExpr* move = toCallExpr(defMap.get(var)->v[0]->parentExpr);
        INT_ASSERT(move && move->isPrimitive(PRIM_MOVE));
        Expr* innermostBlock = NULL;
        // find the innermost block that contains all uses of var
        INT_ASSERT(useMap.get(var));
        forv_Vec(SymExpr, se, *useMap.get(var)) {
          bool useInInnermostBlock = false;
          BlockStmt* curInnermostBlock = toBlockStmt(se->parentExpr);
          INT_ASSERT(!curInnermostBlock); // assumed to be NULL at this point
          for (Expr* block = se->parentExpr->parentExpr;
               block && !useInInnermostBlock;
               block = block->parentExpr) {
            if (!curInnermostBlock)
              curInnermostBlock = toBlockStmt(block);
            if (!innermostBlock) {
              innermostBlock = toBlockStmt(block);
              if (innermostBlock)
                useInInnermostBlock = true;
            } else if (block == innermostBlock)
              useInInnermostBlock = true;
          }
          if (!useInInnermostBlock) {
            // the current use is not contained within innermostBlock,
            // so find out if the innermost block that contains the current use
            // also contains innermostBlock
            Expr* block = innermostBlock;
            while (block && block != curInnermostBlock)
              block = block->parentExpr;
            if (block)
              innermostBlock = curInnermostBlock;
            else {
              // the innermost block that contains the current use is disjoint
              // from the innermost block that contains previously encountered use(s)
              INT_ASSERT(innermostBlock && !block);
              while ((innermostBlock = innermostBlock->parentExpr)) {
                for (block = curInnermostBlock->parentExpr; block && block != innermostBlock;
                     block = block->parentExpr)
                  /* do nothing */;
                if (block) break;
              }
              if (!innermostBlock)
                INT_FATAL(move, "cannot find a block that contains all uses of var\n");
            }
          }
        }
        FnSymbol* fn = toFnSymbol(move->parentSymbol);
        SET_LINENO(var);
        if (fn && innermostBlock == fn->body)
          fn->insertBeforeReturnAfterLabel(callChplHereFree(move->get(1)->copy()));
        else {
          BlockStmt* block = toBlockStmt(innermostBlock);
          INT_ASSERT(block);
          block->insertAtTailBeforeGoto(callChplHereFree(move->get(1)->copy()));
        }
      }
    }
  }
}

// Returns false if 
//  fLocal == true
// or
//  CHPL_COMM == "ugni"
// of
//  CHPL_COMM == "gasnet" && CHPL_GASNET_SEGMENT == "everything";
// true otherwise.
static bool
needHeapVars() {
  if (fLocal) return false;

  if (!strcmp(CHPL_COMM, "ugni") ||
      (!strcmp(CHPL_COMM, "gasnet") &&
       !strcmp(CHPL_GASNET_SEGMENT, "everything")))
    return false;

  return true;
}

//
// In the following, through makeHeapAllocations():
//   refSet, refVec - symbols whose referencees need to be heap-allocated
//   varSet, varVec - symbols that themselves need to be heap-allocated
//

// Traverses all 'begin' or 'on' task functions flagged as needing heap
// allocation (for its formals) or flagged as nonblockikng.
// Traverses all ref formals of these functions and adds them to the refSet and
// refVec.
static void findBlockRefActuals(Vec<Symbol*>& refSet, Vec<Symbol*>& refVec)
{
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_BEGIN) ||
        (fn->hasFlag(FLAG_ON) &&
         (needHeapVars() || fn->hasFlag(FLAG_NON_BLOCKING)))) {
      for_formals(formal, fn) {
        if (formal->type->symbol->hasFlag(FLAG_REF)) {
          refSet.set_add(formal);
          refVec.add(formal);
        }
      }
    }
  }
}


// Traverses all DefExprs.
//  If the symbol is a coforall index expression,
//   If it is of reference type,
//    Add it to refSet and refVec.
//   Otherwise, if it is not of primitive type or other undesired cases,
//    Add it to varSet and varVec.
//  Otherwise, select module-level vars that are not private or extern.
//   If the var is const and has value semantics except record-wrapped types,
//    Insert a prim_private_broadcast call after the def.
//   Otherwise, if it is a record-wrapped type, replicate it.
//   Otherwise,
//    Add it to varSet and varVec, so it will be put on the heap.
static void findHeapVarsAndRefs(Map<Symbol*,Vec<SymExpr*>*>& defMap,
                                Vec<Symbol*>& refSet, Vec<Symbol*>& refVec,
                                Vec<Symbol*>& varSet, Vec<Symbol*>& varVec)
{
  forv_Vec(DefExpr, def, gDefExprs) {
    SET_LINENO(def);
    if (def->sym->hasFlag(FLAG_COFORALL_INDEX_VAR)) {
      if (def->sym->type->symbol->hasFlag(FLAG_REF)) {
        refSet.set_add(def->sym);
        refVec.add(def->sym);
      } else if (!isPrimitiveType(def->sym->type) ||
                 toFnSymbol(def->parentSymbol)->retTag==RET_VAR) {
        varSet.set_add(def->sym);
        varVec.add(def->sym);
      }
    } else if (!fLocal &&
               isModuleSymbol(def->parentSymbol) &&
               def->parentSymbol != rootModule &&
               isVarSymbol(def->sym) &&
               !def->sym->hasFlag(FLAG_PRIVATE) &&
               !def->sym->hasFlag(FLAG_EXTERN)) {
      if (def->sym->hasFlag(FLAG_CONST) &&
          (is_bool_type(def->sym->type) ||
           is_enum_type(def->sym->type) ||
           is_int_type(def->sym->type) ||
           is_uint_type(def->sym->type) ||
           is_real_type(def->sym->type) ||
           is_imag_type(def->sym->type) ||
           is_complex_type(def->sym->type) ||
           (isRecord(def->sym->type) &&
            !isRecordWrappedType(def->sym->type) &&
            // sync/single are currently classes, so this shouldn't matter
            !isSyncType(def->sym->type)))) {
        // replicate global const of primitive type
        INT_ASSERT(defMap.get(def->sym) && defMap.get(def->sym)->n == 1);
        for_defs(se, defMap, def->sym) {
          se->getStmtExpr()->insertAfter(new CallExpr(PRIM_PRIVATE_BROADCAST, def->sym));
        }
      } else if (isRecordWrappedType(def->sym->type)) {
        // replicate address of global arrays, domains, and distributions
        replicateGlobalRecordWrappedVars(def);
      } else {
        // put other global constants and all global variables on the heap
        varSet.set_add(def->sym);
        varVec.add(def->sym);
      }
    }
  }
}


static void
makeHeapAllocations() {
  Vec<Symbol*> refSet;
  Vec<Symbol*> refVec;
  Vec<Symbol*> varSet;
  Vec<Symbol*> varVec;

  Map<Symbol*,Vec<SymExpr*>*> defMap;
  Map<Symbol*,Vec<SymExpr*>*> useMap;
  buildDefUseMaps(defMap, useMap);

  findBlockRefActuals(refSet, refVec);
  findHeapVarsAndRefs(defMap, refSet, refVec, varSet, varVec);

  forv_Vec(Symbol, ref, refVec) {
    if (ArgSymbol* arg = toArgSymbol(ref)) {
      FnSymbol* fn = toFnSymbol(arg->defPoint->parentSymbol);
      forv_Vec(CallExpr, call, *fn->calledBy) {
        SymExpr* se = NULL;
        for_formals_actuals(formal, actual, call) {
          if (formal == arg)
            se = toSymExpr(actual);
        }
        INT_ASSERT(se->var->type->symbol->hasFlag(FLAG_REF));
        if (!refSet.set_in(se->var)) {
          refSet.set_add(se->var);
          refVec.add(se->var);
        }
      }
    } else if (VarSymbol* var = toVarSymbol(ref)) {
      //      INT_ASSERT(defMap.get(var)->n == 1);
      for_defs(def, defMap, var) {
        INT_ASSERT(def);
        if (CallExpr* call = toCallExpr(def->parentExpr)) {
          if (call->isPrimitive(PRIM_MOVE)) {
            if (CallExpr* rhs = toCallExpr(call->get(2))) {
              if (rhs->isPrimitive(PRIM_ADDR_OF)) {
                SymExpr* se = toSymExpr(rhs->get(1));
                INT_ASSERT(se);
                if (!varSet.set_in(se->var)) {
                  varSet.set_add(se->var);
                  varVec.add(se->var);
                }
              } else if (rhs->isPrimitive(PRIM_GET_MEMBER) ||
                         rhs->isPrimitive(PRIM_GET_MEMBER_VALUE) ||
                         rhs->isPrimitive(PRIM_GET_SVEC_MEMBER) ||
                         rhs->isPrimitive(PRIM_GET_SVEC_MEMBER_VALUE)) {
                SymExpr* se = toSymExpr(rhs->get(1));
                INT_ASSERT(se);
                if (se->var->type->symbol->hasFlag(FLAG_REF)) {
                  if (!refSet.set_in(se->var)) {
                    refSet.set_add(se->var);
                    refVec.add(se->var);
                  }
                } else if (!varSet.set_in(se->var)) {
                  varSet.set_add(se->var);
                  varVec.add(se->var);
                }
              }
              //
              // Otherwise assume reference is to something that is
              // already on the heap!  This is concerning...  SJD:
              // Build a future that returns a reference in an
              // iterator to something that is not on the heap
              // (including not in an array).
              //
              // The alternative to making this assumption is to
              // follow the returned reference (assuming this is a
              // function call) through the function and make sure
              // that whatever it returns is on the heap.  Then if we
              // eventually see a GET_ARRAY primitive, we know it is
              // already on the heap.
              //
              // To debug this case, add an else INT_FATAL here.
              //
            } else if (SymExpr* rhs = toSymExpr(call->get(2))) {
              INT_ASSERT(rhs->var->type->symbol->hasFlag(FLAG_REF));
              if (!refSet.set_in(rhs->var)) {
                refSet.set_add(rhs->var);
                refVec.add(rhs->var);
              }
            } else
              INT_FATAL(ref, "unexpected case");
          } else { // !call->isPrimitive(PRIM_MOVE)
            // This definition is created by passing the variable to a function
            // by ref, out or inout intent.  We then assume that the function
            // updates the reference.

            // If the definition of the ref var does not appear in this
            // function, then most likely it was established in an calling
            // routine.
            // We may need to distinguish between definition of the reference
            // var itself (i.e. the establishment of an alias) as compared to
            // when the variable being referenced is updated....
            // In any case, it is safe to ignore this case, because either the
            // value of the ref variable was established elsewhere, or it will
            // appear in another def associated with the ref var.
//            INT_FATAL(ref, "unexpected case");
          }
        } else
          INT_FATAL(ref, "unexpected case");
      }
    }
  }

  Vec<Symbol*> heapAllocatedVars;

  forv_Vec(Symbol, var, varVec) {
    INT_ASSERT(!var->type->symbol->hasFlag(FLAG_REF));

    if (var->hasFlag(FLAG_EXTERN)) {
      // don't widen external variables
      continue;
    }

    if (var->hasFlag(FLAG_PRINT_MODULE_INIT_INDENT_LEVEL)) {
      // don't widen PrintModuleInitOrder variables
      continue;
    }

    if (isModuleSymbol(var->defPoint->parentSymbol)) {
      if (!requireWideReferences()) {
        // don't heap-allocate globals
        continue;
      }
    }

    SET_LINENO(var);

    if (ArgSymbol* arg = toArgSymbol(var)) {
      VarSymbol* tmp = newTemp(var->type);
      varSet.set_add(tmp);
      varVec.add(tmp);
      SymExpr* firstDef = new SymExpr(tmp);
      arg->getFunction()->insertAtHead(new CallExpr(PRIM_MOVE, firstDef, arg));
      addDef(defMap, firstDef);
      arg->getFunction()->insertAtHead(new DefExpr(tmp));
      for_defs(def, defMap, arg) {
        def->var = tmp;
        addDef(defMap, def);
      }
      for_uses(use, useMap, arg) {
        use->var = tmp;
        addUse(useMap, use);
      }
      continue;
    }
    ClassType* heapType = buildHeapType(var->type);

    //
    // allocate local variables on the heap; global variables are put
    // on the heap during program startup
    //
    if (!isModuleSymbol(var->defPoint->parentSymbol) &&
        ((useMap.get(var) && useMap.get(var)->n > 0) ||
         (defMap.get(var) && defMap.get(var)->n > 0))) {
      SET_LINENO(var->defPoint);
      insertChplHereAlloc(var->defPoint->getStmtExpr(),
                          true /*insertAfter*/,var, heapType,
                          newMemDesc("local heap-converted data"));
      heapAllocatedVars.add(var);
    }

    for_defs(def, defMap, var) {
      if (CallExpr* call = toCallExpr(def->parentExpr)) {
        SET_LINENO(call);
        // Do we need a case for PRIM_ASSIGN?
        if (call->isPrimitive(PRIM_MOVE)) {
          VarSymbol* tmp = newTemp(var->type);
          call->insertBefore(new DefExpr(tmp));
          call->insertBefore(new CallExpr(PRIM_MOVE, tmp, call->get(2)->remove()));
          call->replace(new CallExpr(PRIM_SET_MEMBER, call->get(1)->copy(), heapType->getField(1), tmp));
        } else if (call->isResolved() &&
                   call->isResolved()->hasFlag(FLAG_AUTO_DESTROY_FN)) {
          call->remove();
        } else {
          VarSymbol* tmp = newTemp(var->type);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_MEMBER_VALUE, def->var, heapType->getField(1))));
          def->replace(new SymExpr(tmp));
        }
      } else
        INT_FATAL(var, "unexpected case");
    }

    for_uses(use, useMap, var) {
      if (CallExpr* call = toCallExpr(use->parentExpr)) {
        if (call->isPrimitive(PRIM_ADDR_OF)) {
          CallExpr* move = toCallExpr(call->parentExpr);
          INT_ASSERT(move && move->isPrimitive(PRIM_MOVE));
          if (move->get(1)->typeInfo() == heapType) {
            call->replace(use->copy());
          } else {
            call->replace(new CallExpr(PRIM_GET_MEMBER, use->var, heapType->getField(1)));
          }
        } else if (call->isResolved()) {
          if (call->isResolved()->hasFlag(FLAG_AUTO_DESTROY_FN_SYNC)) {
            //
            // We don't move sync vars to the heap and don't do the
            // analysis to determine whether or not they outlive a
            // task that refers to them, so conservatively remove
            // their autodestroy calls to avoid freeing them before
            // all tasks are done with them.  While this is
            // unfortunate and needs to be fixed in the future to
            // avoid leaks (TODO), it is better than the previous
            // version of this code that would remove all autodestroy
            // calls in this conditional.  See the commit message for
            // this commenet for more detail.
            //
            call->remove();
          } else if (actual_to_formal(use)->type == heapType) {
            // do nothing
          } else {
            VarSymbol* tmp = newTemp(var->type);
            call->getStmtExpr()->insertBefore(new DefExpr(tmp));
            call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_MEMBER_VALUE, use->var, heapType->getField(1))));
            use->replace(new SymExpr(tmp));
          }
        } else if ((call->isPrimitive(PRIM_GET_MEMBER) ||
                    call->isPrimitive(PRIM_GET_SVEC_MEMBER) ||
                    call->isPrimitive(PRIM_GET_MEMBER_VALUE) ||
                    call->isPrimitive(PRIM_GET_SVEC_MEMBER_VALUE) ||
                    call->isPrimitive(PRIM_WIDE_GET_LOCALE) || //I'm not sure this is cricket.
                    call->isPrimitive(PRIM_WIDE_GET_NODE) ||// what member are we extracting?
                    call->isPrimitive(PRIM_SET_SVEC_MEMBER) ||
                    call->isPrimitive(PRIM_SET_MEMBER)) &&
                   call->get(1) == use) {
          VarSymbol* tmp = newTemp(var->type->refType);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_MEMBER, use->var, heapType->getField(1))));
          use->replace(new SymExpr(tmp));
        } else {
          VarSymbol* tmp = newTemp(var->type);
          call->getStmtExpr()->insertBefore(new DefExpr(tmp));
          call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_MEMBER_VALUE, use->var, heapType->getField(1))));
          use->replace(new SymExpr(tmp));
        }
      } else if (use->parentExpr)
        INT_FATAL(var, "unexpected case");
    }

    var->type = heapType;
  }

  freeHeapAllocatedVars(heapAllocatedVars);
}


//
// re-privatize privatized object fields in iterator classes
//
static void
reprivatizeIterators() {
  if (fLocal)
    return; // no need for privatization

  Vec<Symbol*> privatizedFields;

  forv_Vec(ClassType, ct, gClassTypes) {
    for_fields(field, ct) {
      if (ct->symbol->hasFlag(FLAG_ITERATOR_CLASS) &&
          field->type->symbol->hasFlag(FLAG_PRIVATIZED_CLASS)) {
        privatizedFields.set_add(field);
      }
    }
  }

  forv_Vec(SymExpr, se, gSymExprs) {
    if (privatizedFields.set_in(se->var)) {
      SET_LINENO(se);
      if (CallExpr* call = toCallExpr(se->parentExpr)) {
        if (call->isPrimitive(PRIM_GET_MEMBER_VALUE)) {
          CallExpr* move = toCallExpr(call->parentExpr);
          INT_ASSERT(move->isPrimitive(PRIM_MOVE));
          SymExpr* lhs = toSymExpr(move->get(1));
          ClassType* ct = toClassType(se->var->type);
          VarSymbol* tmp = newTemp(ct->getField("pid")->type);
          move->insertBefore(new DefExpr(tmp));
          lhs->replace(new SymExpr(tmp));
          move->insertAfter(new CallExpr(PRIM_MOVE, lhs->var, new CallExpr(PRIM_GET_PRIV_CLASS, lhs->var->type->symbol, tmp)));
        } else if (call->isPrimitive(PRIM_GET_MEMBER)) {
          CallExpr* move = toCallExpr(call->parentExpr);
          INT_ASSERT(move->isPrimitive(PRIM_MOVE));
          SymExpr* lhs = toSymExpr(move->get(1));
          ClassType* ct = toClassType(se->var->type);
          VarSymbol* tmp = newTemp(ct->getField("pid")->type);
          move->insertBefore(new DefExpr(tmp));
          lhs->replace(new SymExpr(tmp));
          call->primitive = primitives[PRIM_GET_MEMBER_VALUE];
          VarSymbol* valTmp = newTemp(lhs->getValType());
          move->insertBefore(new DefExpr(valTmp));
          move->insertAfter(new CallExpr(PRIM_MOVE, lhs, new CallExpr(PRIM_ADDR_OF, valTmp)));
          move->insertAfter(new CallExpr(PRIM_MOVE, valTmp, new CallExpr(PRIM_GET_PRIV_CLASS, lhs->getValType()->symbol, tmp)));
        } else if (call->isPrimitive(PRIM_SET_MEMBER)) {
          ClassType* ct = toClassType(se->var->type);
          VarSymbol* tmp = newTemp(ct->getField("pid")->type);
          call->insertBefore(new DefExpr(tmp));
          call->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_GET_MEMBER_VALUE, call->get(3)->remove(), ct->getField("pid"))));
          call->insertAtTail(new SymExpr(tmp));
        } else
          INT_FATAL(se, "unexpected case in re-privatization in iterator");
      } else
        INT_FATAL(se, "unexpected case in re-privatization in iterator");
    }
  }

  forv_Vec(Symbol, sym, privatizedFields) if (sym) {
    sym->type = dtInt[INT_SIZE_DEFAULT];
  }
}


void
parallel(void) {
  Vec<FnSymbol*> taskFunctions;

  // Collect the task functions for processing.
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (isTaskFun(fn)) {
      taskFunctions.add(fn);
      // Would need to flatten them if they are not already.
      INT_ASSERT(isGlobal(fn));
    }
  }

  compute_call_sites();

  // TODO: Move this into a separate pass.
  remoteValueForwarding(taskFunctions);

  reprivatizeIterators();

  makeHeapAllocations();

  insertEndCounts();

  passArgsToNestedFns(taskFunctions);
}


static void insertEndCounts()
{
  Vec<FnSymbol*> queue;
  Map<FnSymbol*,Symbol*> endCountMap;

  forv_Vec(CallExpr, call, gCallExprs) {
    SET_LINENO(call);
    if (call->isPrimitive(PRIM_GET_END_COUNT)) {
      FnSymbol* pfn = call->getFunction();
      if (!endCountMap.get(pfn))
        insertEndCount(pfn, call->typeInfo(), queue, endCountMap);
      call->replace(new SymExpr(endCountMap.get(pfn)));
    } else if (call->isPrimitive(PRIM_SET_END_COUNT)) {
      FnSymbol* pfn = call->getFunction();
      if (!endCountMap.get(pfn))
        insertEndCount(pfn, call->get(1)->typeInfo(), queue, endCountMap);
      call->replace(new CallExpr(PRIM_MOVE, endCountMap.get(pfn), call->get(1)->remove()));
    }
  }

  forv_Vec(FnSymbol, fn, queue) {
    forv_Vec(CallExpr, call, *fn->calledBy) {
      SET_LINENO(call);
      Type* endCountType = endCountMap.get(fn)->type;
      FnSymbol* pfn = call->getFunction();
      if (!endCountMap.get(pfn))
        insertEndCount(pfn, endCountType, queue, endCountMap);
      call->insertAtTail(endCountMap.get(pfn));
    }
  }
}


// For each "nested" function created to represent remote execution, 
// bundle args so they can be passed through a fork function.
// Fork functions in general have the signature
//  fork(int32_t destNode, void (*)(void* args), void* args, ...);
// In Chapel, we wrap the arguments passed to the nested function in an object
// whose type is just a list of the arguments passed to the nested function.
// Those arguments consist of variables in the scope of the nested function call
// that are accessed within the body of the nested function (recursively, of course).
static void passArgsToNestedFns(Vec<FnSymbol*>& nestedFunctions)
{
  forv_Vec(FnSymbol, fn, nestedFunctions) {

    BundleArgsFnData baData = bundleArgsFnDataInit;

    forv_Vec(CallExpr, call, *fn->calledBy) {
      SET_LINENO(call);
      bundleArgs(call, baData);
    }

    if (fn->hasFlag(FLAG_ON))
    {
      // Now we can remove the dummy locale arg from the on_fn
      DefExpr* localeArg = toDefExpr(fn->formals.get(1));
      std::vector<SymExpr*> symExprs;
      collectSymExprsSTL(fn->body, symExprs);
      for_vector(SymExpr, sym, symExprs)
      {
        if (sym->var->defPoint == localeArg)
          sym->getStmtExpr()->remove();
      }
      localeArg->remove();
    }
  }
}


ClassType* wideStringType = NULL;

static void
buildWideClass(Type* type) {
  SET_LINENO(type->symbol);
  ClassType* wide = new ClassType(CLASS_RECORD);
  TypeSymbol* wts = new TypeSymbol(astr("__wide_", type->symbol->cname), wide);
  wts->addFlag(FLAG_WIDE_CLASS);
  theProgram->block->insertAtTail(new DefExpr(wts));
  wide->fields.insertAtTail(new DefExpr(new VarSymbol("locale", dtLocaleID)));
  wide->fields.insertAtTail(new DefExpr(new VarSymbol("addr", type)));

  //
  // Strings need an extra field in their wide class to hold their length
  //
  if (type == dtString) {
    wide->fields.insertAtTail(new DefExpr(new VarSymbol("size", dtInt[INT_SIZE_DEFAULT])));
    if (wideStringType) {
      INT_FATAL("Created two wide string types");
    }
    wideStringType = wide;
  }

  //
  // set reference type of wide class to reference type of class since
  // it will be widened
  //
  if (type->refType)
    wide->refType = type->refType;

  wideClassMap.put(type, wide);
}

Type* getOrMakeRefTypeDuringCodegen(Type* type) {
  Type* refType;
  refType = type->refType;
  if( ! refType ) {
    SET_LINENO(type->symbol);
    ClassType* ref = new ClassType(CLASS_RECORD);
    TypeSymbol* refTs = new TypeSymbol(astr("_ref_", type->symbol->cname), ref);
    refTs->addFlag(FLAG_REF);
    refTs->addFlag(FLAG_NO_DEFAULT_FUNCTIONS);
    refTs->addFlag(FLAG_NO_OBJECT);
    theProgram->block->insertAtTail(new DefExpr(refTs));
    ref->fields.insertAtTail(new DefExpr(new VarSymbol("_val", type)));
    refType = ref;
    type->refType = ref;
  }
  return refType;
}

// This function is called if the wide reference type does not already
// exist to cause it to be code generated even though it was not
// needed by earlier passes.
Type* getOrMakeWideTypeDuringCodegen(Type* refType) {
  Type* wideType;
  INT_ASSERT(refType == dtNil ||
             isClass(refType) ||
             refType->symbol->hasFlag(FLAG_REF));
  // First, check if the wide type already exists.
  if( isClass(refType) ) {
    wideType = wideClassMap.get(refType);
    if( wideType ) return wideType;
  }
  // For a ref to a class, isClass seems to return true...
  wideType = wideRefMap.get(refType);
  if( wideType ) return wideType;

  // Now, create a wide pointer type.
  ClassType* wide = new ClassType(CLASS_RECORD);
  TypeSymbol* wts = new TypeSymbol(astr("chpl____wide_", refType->symbol->cname), wide);
  if( refType->symbol->hasFlag(FLAG_REF) || refType == dtNil )
    wts->addFlag(FLAG_WIDE);
  else
    wts->addFlag(FLAG_WIDE_CLASS);
  theProgram->block->insertAtTail(new DefExpr(wts));
  wide->fields.insertAtTail(new DefExpr(new VarSymbol("locale", dtLocaleID)));
  wide->fields.insertAtTail(new DefExpr(new VarSymbol("addr", refType)));
  if( isClass(refType) ) {
    wideClassMap.put(refType, wide);
  } else {
    wideRefMap.put(refType, wide);
  }
  return wide;
}

 
//
// Returns true if the type t is a reference to a wide string
//
// This is used to handle cases where wide strings are passed to
// functions that require local arguments.  If strings were a little
// better behaved, it arguably wouldn't/shouldn't be required.
//
bool isRefWideString(Type* t) {
  if (isReferenceType(t)) {
    ClassType* ct = toClassType(t);
    INT_ASSERT(ct);
    Symbol* valField = ct->getField("_val", false);
    INT_ASSERT(valField);
    return isWideString(valField->type);
  }
  return false;
}

bool isWideString(Type* t) {
  if (!requireWideReferences()) return false; // no wide string type will exist if wide references weren't created
  INT_ASSERT(wideStringType); // should only be called after it exists!
  if (t == NULL) return false;
  if( t == wideStringType ) return true;
  return false;
}

//
// The argument expr is a use of a wide reference. Insert a check to ensure
// that it is on the current locale, then drop its wideness by moving the
// addr field into a non-wide of otherwise the same type. Then, replace its
// use with the non-wide version.
//
static void insertLocalTemp(Expr* expr) {
  SymExpr* se = toSymExpr(expr);
  Expr* stmt = expr->getStmtExpr();
  INT_ASSERT(se && stmt);
  SET_LINENO(se);
  VarSymbol* var = newTemp(astr("local_", se->var->name),
                           se->var->type->getField("addr")->type);
  if (!fNoLocalChecks) {
    stmt->insertBefore(new CallExpr(PRIM_LOCAL_CHECK, se->copy()));
  }
  stmt->insertBefore(new DefExpr(var));
  stmt->insertBefore(new CallExpr(PRIM_MOVE, var, se->copy()));
  se->replace(new SymExpr(var));
}


//
// If call has the potential to cause communication, assert that the wide
// reference that might cause communication is local and remove its wide-ness
//
// The organization of this function follows the order of CallExpr::codegen()
// leaving out primitives that don't communicate.
//
static void localizeCall(CallExpr* call) {
  if (call->primitive) {
    switch (call->primitive->tag) {
    case PRIM_ARRAY_SET: /* Fallthru */
    case PRIM_ARRAY_SET_FIRST:
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        insertLocalTemp(call->get(1));
      }
      break;
    case PRIM_MOVE:
    case PRIM_ASSIGN: // Not sure about this one.
      if (CallExpr* rhs = toCallExpr(call->get(2))) {
        if (rhs->isPrimitive(PRIM_DEREF)) {
          if (rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) ||
              rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            insertLocalTemp(rhs->get(1));
            if (!rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
              INT_ASSERT(rhs->get(1)->typeInfo() == dtString);
              // special handling for wide strings
              rhs->replace(rhs->get(1)->remove());
            }
          }
          break;
        } 
        else if (rhs->isPrimitive(PRIM_GET_MEMBER) ||
                   rhs->isPrimitive(PRIM_GET_SVEC_MEMBER) ||
                   rhs->isPrimitive(PRIM_GET_MEMBER_VALUE) ||
                   rhs->isPrimitive(PRIM_GET_SVEC_MEMBER_VALUE)) {
          if (rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) ||
              rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            SymExpr* sym = toSymExpr(rhs->get(2));
            INT_ASSERT(sym);
            if (!sym->var->hasFlag(FLAG_SUPER_CLASS)) {
              insertLocalTemp(rhs->get(1));
            }
          }
          break;
        } else if (rhs->isPrimitive(PRIM_ARRAY_GET) ||
                   rhs->isPrimitive(PRIM_ARRAY_GET_VALUE)) {
          if (rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            SymExpr* lhs = toSymExpr(call->get(1));
            Expr* stmt = call->getStmtExpr();
            INT_ASSERT(lhs && stmt);

            SET_LINENO(stmt);
            insertLocalTemp(rhs->get(1));
            VarSymbol* localVar = NULL;
            if (rhs->isPrimitive(PRIM_ARRAY_GET))
              localVar = newTemp(astr("local_", lhs->var->name),
                                 lhs->var->type->getField("addr")->type);
            else
              localVar = newTemp(astr("local_", lhs->var->name),
                                 lhs->var->type);
            stmt->insertBefore(new DefExpr(localVar));
            lhs->replace(new SymExpr(localVar));
            stmt->insertAfter(new CallExpr(PRIM_MOVE, lhs,
                                           new SymExpr(localVar)));
          }
          break;
        } else if (rhs->isPrimitive(PRIM_GET_UNION_ID)) {
          if (rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
            insertLocalTemp(rhs->get(1));
          }
          break;
        } else if (rhs->isPrimitive(PRIM_TESTCID) ||
                   rhs->isPrimitive(PRIM_GETCID)) {
          if (rhs->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            insertLocalTemp(rhs->get(1));
          }
          break;
        }
        ;
      }
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          !call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        break;
      }
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) &&
          !call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE) &&
          !call->get(2)->typeInfo()->symbol->hasFlag(FLAG_REF)) {
        insertLocalTemp(call->get(1));
      }
      break;
    case PRIM_DYNAMIC_CAST:
      if (call->get(2)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        insertLocalTemp(call->get(2));
        if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
            call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
          toSymExpr(call->get(1))->var->type = call->get(1)->typeInfo()->getField("addr")->type;
        }
      }
      break;
    case PRIM_SETCID:
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        insertLocalTemp(call->get(1));
      }
      break;
    case PRIM_SET_UNION_ID:
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        insertLocalTemp(call->get(1));
      }
      break;
    case PRIM_SET_MEMBER:
    case PRIM_SET_SVEC_MEMBER:
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS) ||
          call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE)) {
        insertLocalTemp(call->get(1));
      }
      break;
    default:
      break;
    }
  }
}


//
// Do a breadth first search starting from functions generated for local blocks
// for all function calls in each level of the search, if they directly cause
// communication, add a local temp that isn't wide. If it is a resolved call,
// meaning that it isn't a primitive or external function, clone it and add it
// to the queue of functions to handle at the next iteration of the BFS.
//
static void handleLocalBlocks() {
  Map<FnSymbol*,FnSymbol*> cache; // cache of localized functions
  Vec<BlockStmt*> queue; // queue of blocks to localize

  forv_Vec(BlockStmt, block, gBlockStmts) {
    if (block->parentSymbol)
      if (block->blockInfo)
        if (block->blockInfo->isPrimitive(PRIM_BLOCK_LOCAL))
          queue.add(block);
  }

  forv_Vec(BlockStmt, block, queue) {
    Vec<CallExpr*> calls;
    collectCallExprs(block, calls);
    forv_Vec(CallExpr, call, calls) {
      localizeCall(call);
      if (FnSymbol* fn = call->isResolved()) {
        SET_LINENO(fn);
        if (FnSymbol* alreadyLocal = cache.get(fn)) {
          call->baseExpr->replace(new SymExpr(alreadyLocal));
        } else {
          if (!fn->hasFlag(FLAG_EXTERN)) {
            FnSymbol* local = fn->copy();
            local->addFlag(FLAG_LOCAL_FN);
            local->name = astr("_local_", fn->name);
            local->cname = astr("_local_", fn->cname);
            fn->defPoint->insertBefore(new DefExpr(local));
            call->baseExpr->replace(new SymExpr(local));
            queue.add(local->body);
            cache.put(fn, local);
            cache.put(local, local); // to handle recursion
            if (local->retType->symbol->hasFlag(FLAG_WIDE)) {
              CallExpr* ret = toCallExpr(local->body->body.tail);
              INT_ASSERT(ret && ret->isPrimitive(PRIM_RETURN));
              // Capture the return expression in a local temp.
              insertLocalTemp(ret->get(1));
              local->retType = ret->get(1)->typeInfo();
            }
          }
        }
      }
    }
  }
}


// Add symbols bearing the FLAG_HEAP flag to a list of heapVars.
static void getHeapVars(Vec<Symbol*>& heapVars)
{
  // Look at all def expressions.
  forv_Vec(DefExpr, def, gDefExprs)
  {
    // We are interested only in var symbols.
    if (!isVarSymbol(def->sym))
      continue;

    // We only want symbols at the module level.
    if (!isModuleSymbol(def->parentSymbol))
      continue;

    // But we don't want any from the root module.
    if (def->parentSymbol == rootModule)
      continue;

    // Okey-dokey.  List up those heap variables.
    if (def->sym->type->symbol->hasFlag(FLAG_HEAP))
      heapVars.add(def->sym);
  }
}


// Create chpl__heapAllocateGlobals and stub it in.
// If the program does not require wide reference, it will be empty.
// In that case, add a "return void;" statement to make the function normal.
// The stub is returned, so it can be completed by heapAllocateGlobalsTail().
static FnSymbol* heapAllocateGlobalsHead()
{
  SET_LINENO(baseModule);
  FnSymbol* heapAllocateGlobals = new FnSymbol("chpl__heapAllocateGlobals");
  heapAllocateGlobals->addFlag(FLAG_EXPORT);
  heapAllocateGlobals->addFlag(FLAG_LOCAL_ARGS);
  heapAllocateGlobals->retType = dtVoid;
  theProgram->block->insertAtTail(new DefExpr(heapAllocateGlobals));

  // Abbreviated version if we are not using wide references.
  // heapAllocateGlobalsTail() is only called if requireWideReferences() returns
  // true.
  if (!requireWideReferences())
    heapAllocateGlobals->insertAtTail(new CallExpr(PRIM_RETURN, gVoid));
  return heapAllocateGlobals;
}


static void heapAllocateGlobalsTail(FnSymbol* heapAllocateGlobals,
                                    Vec<Symbol*> heapVars)
{
  SET_LINENO(baseModule);
  
  SymExpr* nodeID = new SymExpr(gNodeID);
  VarSymbol* tmp = newTemp(gNodeID->type);
  VarSymbol* tmpBool = newTemp(dtBool);

  heapAllocateGlobals->insertAtTail(new DefExpr(tmp));
  heapAllocateGlobals->insertAtTail(new DefExpr(tmpBool));
  heapAllocateGlobals->insertAtTail(new CallExpr(PRIM_MOVE, tmp, nodeID));
  heapAllocateGlobals->insertAtTail(new CallExpr(PRIM_MOVE, tmpBool, new CallExpr(PRIM_EQUAL, tmp, new_IntSymbol(0))));
  BlockStmt* block = new BlockStmt();
  DefExpr *dummy = new DefExpr(newTemp());
  block->insertAtTail(dummy);
  forv_Vec(Symbol, sym, heapVars) {
    insertChplHereAlloc(dummy, false /*insertAfter*/, sym,
                        sym->type->getField("addr")->type,
                        newMemDesc("global heap-converted data"));
  }
  dummy->remove();
  heapAllocateGlobals->insertAtTail(new CondStmt(new SymExpr(tmpBool), block));
  int i = 0;
  forv_Vec(Symbol, sym, heapVars) {
    heapAllocateGlobals->insertAtTail(new CallExpr(PRIM_HEAP_REGISTER_GLOBAL_VAR, new_IntSymbol(i++), sym));
  }
  heapAllocateGlobals->insertAtTail(new CallExpr(PRIM_HEAP_BROADCAST_GLOBAL_VARS, new_IntSymbol(i)));
  heapAllocateGlobals->insertAtTail(new CallExpr(PRIM_RETURN, gVoid));
  numGlobalsOnHeap = i;
}


//
// change all classes into wide classes
// change all references into wide references
//
void
insertWideReferences(void) {
  FnSymbol* heapAllocateGlobals = heapAllocateGlobalsHead();

  if (!requireWideReferences())
    return;

  // TODO: Can this declaration and initialization be moved closer to where it
  // is used?
  Vec<Symbol*> heapVars;
  getHeapVars(heapVars);

  convertNilToObject();

  INT_ASSERT(wideClassMap.n == 0);
  buildWideClasses();
  widenClasses();

  INT_ASSERT(wideRefMap.n == 0);
  buildWideRefMap();
  widenRefs();

  insertElementAccessTemps();
  narrowWideClassesThroughCalls();
  insertWideClassTempsForNil();
  insertWideCastTemps();
  derefWideStringActuals();
  derefWideRefsToWideClasses();
  widenGetPrivClass();
  heapAllocateGlobalsTail(heapAllocateGlobals, heapVars);
  handleLocalBlocks();
  narrowWideReferences();

  // TODO: Test if this step is really necessary.  If it is, document why.
  moveAddressSourcesToTemp();
}


// Convert dtNil to dtObject.
// dtNil is a special type (like void*) that can be converted to any class type.
static void convertNilToObject()
{
  forv_Vec(DefExpr, def, gDefExprs) {
    // Note that FnSymbols, VarSymbols and ArgSymbols are disjoint sets, so in
    // each iteration of this loop, at most one of the following two "if"
    // clauses will execute.

    // change dtNil return type into dtObject
    if (FnSymbol* fn = toFnSymbol(def->sym)) {
      if (fn->retType == dtNil)
        fn->retType = dtObject;
    }

    // replace symbols of type nil by nil
    if (isVarSymbol(def->sym) || isArgSymbol(def->sym)) {
      if (def->sym->type == dtNil &&
          !isTypeSymbol(def->parentSymbol))
        if (def->sym != gNil) // TODO: Do we need this test?  If so, document why.
          def->remove();
    }
  }

  // This replaces vars of type dtNil with gNil.
  // Also, if that var is the LHS of a move, remove the move (since it is no
  // longer used).
  forv_Vec(SymExpr, se, gSymExprs) {
    if (se->var->type == dtNil) {
      se->var = gNil;
      if (CallExpr* parent = toCallExpr(se->parentExpr))
        // Assignment to void should already have been flagged as an error.
        if (parent->isPrimitive(PRIM_MOVE) && parent->get(1) == se)
          parent->remove();
    }
  }
}


static void buildWideClasses()
{
  //
  // build a wide class type for every class type
  //
  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    ClassType* ct = toClassType(ts->type);
    if (ct && isClass(ct) && !ts->hasFlag(FLAG_REF) && !ts->hasFlag(FLAG_NO_WIDE_CLASS)) {
      buildWideClass(ct);
    }
  }
  buildWideClass(dtString);
}


// TODO: It might be better to call this "widenClassTypes()".
static void widenClasses()
{
  //
  // change all class references into wide class references.
  //
  forv_Vec(DefExpr, def, gDefExprs) {
    //
    // do not widen literals
    //
    if (VarSymbol* var = toVarSymbol(def->sym))
      if (var->immediate)
        continue;

    //
    // do not change the class field in a wide class type
    //
    if (TypeSymbol* ts = toTypeSymbol(def->parentSymbol))
      if (ts->hasFlag(FLAG_WIDE_CLASS))
        continue;

    //
    // do not change super class field - it's really a record
    //
    if (def->sym->hasFlag(FLAG_SUPER_CLASS))
      continue;

    // Note that the following two "if" statements are mutually exclusive.

    // Widen the return type of every function
    // except those marked "local args".
    if (FnSymbol* fn = toFnSymbol(def->sym)) {
      if (!fn->hasEitherFlag(FLAG_EXTERN,FLAG_LOCAL_ARGS))
        if (Type* wide = wideClassMap.get(fn->retType))
          fn->retType = wide;
    }

    // Widen all variables, 
    // and all arguments of functions not marked "extern".
    if (isVarSymbol(def->sym) || isArgSymbol(def->sym))
    {
      if (Type* wide = wideClassMap.get(def->sym->type))
        if (isVarSymbol(def->sym) ||
            !def->parentSymbol->hasFlag(FLAG_EXTERN))
          def->sym->type = wide;
    }
  }

  //
  // change arrays of classes into arrays of wide classes
  //
  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if (ts->hasFlag(FLAG_DATA_CLASS)) {
      if (Type* nt = wideClassMap.get(getDataClassType(ts)->type)) {
        setDataClassType(ts, nt->symbol);
      }
    }
  }
}


// Build a wide reference type from every reference type
// and build a map from the narrow ref type to its corresponding wide ref type.
static void buildWideRefMap()
{
  //
  // build wide reference type for every reference type
  //
  forv_Vec(TypeSymbol, ts, gTypeSymbols) {
    if (ts->hasFlag(FLAG_REF)) {
      SET_LINENO(ts);

      ClassType* wide = new ClassType(CLASS_RECORD);
      TypeSymbol* wts = new TypeSymbol(astr("__wide_", ts->cname), wide);
      wts->addFlag(FLAG_WIDE);
      theProgram->block->insertAtTail(new DefExpr(wts));
      wide->fields.insertAtTail(new DefExpr(new VarSymbol("locale", dtLocaleID)));
      wide->fields.insertAtTail(new DefExpr(new VarSymbol("addr", ts->type)));

      wideRefMap.put(ts->type, wide);
    }
  }
}


// Change all references into wide references.
static void widenRefs()
{
  //
  // change all references into wide references
  //
  forv_Vec(DefExpr, def, gDefExprs) {
    //
    // do not change the reference field in a wide reference type
    //
    if (TypeSymbol* ts = toTypeSymbol(def->parentSymbol))
      if (ts->hasFlag(FLAG_WIDE))
        continue;

    //
    // do not change super field - it's really a record
    //
    if (def->sym->hasFlag(FLAG_SUPER_CLASS))
      continue;

    // Note that the following two "if" statements are mutually exclusive.

    // Change ref types on function return values to wide ref types.
    if (FnSymbol* fn = toFnSymbol(def->sym)) {
      if (Type* wide = wideRefMap.get(fn->retType))
        fn->retType = wide;
    }

    // Widen all variables and arguments of reference type.
    if (isVarSymbol(def->sym) || isArgSymbol(def->sym))
    {
      if (Type* wide = wideRefMap.get(def->sym->type))
        def->sym->type = wide;
    }
  }
}


static void insertElementAccessTemps()
{
  //
  // Special case string literals passed to functions, set member primitives
  // and array element initializers by pushing them into temps first.
  //
  forv_Vec(SymExpr, se, gSymExprs) {
    if (se->var->type == dtString) {
      if (VarSymbol* var = toVarSymbol(se->var)) {
        if (var->immediate) {
          if (CallExpr* call = toCallExpr(se->parentExpr)) {
            SET_LINENO(se);
            if (call->isResolved())
            {
              if (!call->isResolved()->hasEitherFlag(FLAG_EXTERN,FLAG_LOCAL_ARGS)) {
                if (Type* type = actual_to_formal(se)->typeInfo()) {
                  VarSymbol* tmp = newTemp(type);
                  call->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->replace(new SymExpr(tmp));
                  call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
                }
              }
            }
            else // isResolved() is false for primitives.
            {
              if (call->isPrimitive(PRIM_VMT_CALL)) {
                if (Type* type = actual_to_formal(se)->typeInfo()) {
                  VarSymbol* tmp = newTemp(type);
                  call->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->replace(new SymExpr(tmp));
                  call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
                }
              }
              if (call->isPrimitive(PRIM_SET_MEMBER)) {
                if (SymExpr* wide = toSymExpr(call->get(2))) {
                  Type* type = wide->var->type;
                  VarSymbol* tmp = newTemp(type);
                  call->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->replace(new SymExpr(tmp));
                  call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
                }
              }
              if (call->isPrimitive(PRIM_SET_SVEC_MEMBER)) {
                Type* valueType = call->get(1)->getValType();
                Type* componentType = valueType->getField("x1")->type;
                if (componentType->symbol->hasFlag(FLAG_WIDE_CLASS)) {
                  VarSymbol* tmp = newTemp(componentType);
                  call->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->replace(new SymExpr(tmp));
                  call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
                }
              }
              if (call->isPrimitive(PRIM_ARRAY_SET_FIRST)) {
                if (SymExpr* wide = toSymExpr(call->get(3))) {
                  Type* type = wide->var->type;
                  VarSymbol* tmp = newTemp(wideClassMap.get(type));
                  call->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->replace(new SymExpr(tmp));
                  call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
                }
              }
            }
          }
        }
      }
    }
  }
}


static void narrowWideClassesThroughCalls()
{
  //
  // Turn calls to functions with local arguments (e.g. extern or export
  // functions) involving wide classes
  // into moves of the wide class into a non-wide type and then use
  // that in the call.  After the call, copy the value back into the
  // wide class.
  //
  forv_Vec(CallExpr, call, gCallExprs) {

    // Find calls to functions expecting local arguments.
    if (call->isResolved() && call->isResolved()->hasFlag(FLAG_LOCAL_ARGS)) {
      SET_LINENO(call);

      // Examine each argument to the call.
      for_alist(arg, call->argList) {

        SymExpr* sym = toSymExpr(arg);
        INT_ASSERT(sym);
        Type* symType = sym->typeInfo();

        // Select symbols with wide types.
        if (symType->symbol->hasFlag(FLAG_WIDE_CLASS) ||
            symType->symbol->hasFlag(FLAG_WIDE)) {
          Type* narrowType = symType->getField("addr")->type;

          // Copy 
          VarSymbol* var = newTemp(narrowType);
          SET_LINENO(call);
          call->getStmtExpr()->insertBefore(new DefExpr(var));

          
          if ((symType->symbol->hasFlag(FLAG_WIDE_CLASS) &&
               narrowType->symbol->hasFlag(FLAG_EXTERN)) ||
              isRefWideString(narrowType)) {

            // Insert a local check because we cannot reflect any changes
            // made to the class back to another locale
            if (!fNoLocalChecks)
              call->getStmtExpr()->insertBefore(new CallExpr(PRIM_LOCAL_CHECK, sym->copy()));

            // If we pass an extern class to an extern/export function,
            // we must treat it like a reference (this is by definition)
            call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, var, sym->copy()));
          } 
          else if (narrowType->symbol->hasEitherFlag(FLAG_REF,FLAG_DATA_CLASS))
            // Also if the narrow type is a ref or data class type,
            // we must treat it like a (narrow) reference.
            call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, var, sym->copy()));
          else
            // Otherwise, narrow the wide class reference, and use that in the call
            call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, var, new CallExpr(PRIM_DEREF, sym->copy())));

          // Move the result back after the call.
          call->getStmtExpr()->insertAfter(new CallExpr(PRIM_MOVE, sym->copy(), var));
          sym->replace(new SymExpr(var));
        }
      }
    }
  }
}


static void insertWideClassTempsForNil()
{
  //
  // insert wide class temps for nil
  //
  forv_Vec(SymExpr, se, gSymExprs) {
    if (se->var == gNil) {
      if (CallExpr* call = toCallExpr(se->parentExpr)) {
        SET_LINENO(se);
        if (call->isResolved()) {
          if (Type* type = actual_to_formal(se)->typeInfo()) {
            if (type->symbol->hasFlag(FLAG_WIDE_CLASS)) {
              VarSymbol* tmp = newTemp(type);
              call->getStmtExpr()->insertBefore(new DefExpr(tmp));
              se->replace(new SymExpr(tmp));
              call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
            }
          }
        } else if (call->isPrimitive(PRIM_MOVE)) {
          if (Type* wtype = call->get(1)->typeInfo()) {
            if (wtype->symbol->hasFlag(FLAG_WIDE)) {
              if (Type* wctype = wtype->getField("addr")->type->getField("_val")->type) {
                if (wctype->symbol->hasFlag(FLAG_WIDE_CLASS)) {
                  VarSymbol* tmp = newTemp(wctype);
                  call->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->replace(new SymExpr(tmp));
                  call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
                }
              }
            }
          }
        } else if (call->isPrimitive(PRIM_SET_MEMBER)) {
          if (Type* wctype = call->get(2)->typeInfo()) {
            if (wctype->symbol->hasFlag(FLAG_WIDE_CLASS) ||
                wctype->symbol->hasFlag(FLAG_WIDE)) {
              VarSymbol* tmp = newTemp(wctype);
              call->insertBefore(new DefExpr(tmp));
              se->replace(new SymExpr(tmp));
              call->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
            }
          }
        } else if (call->isPrimitive(PRIM_SET_SVEC_MEMBER)) {
          Type* valueType = call->get(1)->getValType();
          Type* componentType = valueType->getField("x1")->type;
          if (componentType->symbol->hasFlag(FLAG_WIDE_CLASS) ||
              componentType->symbol->hasFlag(FLAG_WIDE)) {
            VarSymbol* tmp = newTemp(componentType);
            call->insertBefore(new DefExpr(tmp));
            se->replace(new SymExpr(tmp));
            call->insertBefore(new CallExpr(PRIM_MOVE, tmp, se));
          }
        } else if (call->isPrimitive(PRIM_RETURN)) {
          FnSymbol* fn = toFnSymbol(call->parentSymbol);
          INT_ASSERT(fn);
          VarSymbol* tmp = newTemp(fn->retType);
          call->insertBefore(new DefExpr(tmp));
          call->insertBefore(new CallExpr(PRIM_MOVE, tmp, gNil));
          se->var = tmp;
        }
      }
    }
  }
}


static void insertWideCastTemps()
{
  //
  // insert cast temps if lhs type does not match cast type
  //   allows separation of the remote put with the wide cast
  //
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->isPrimitive(PRIM_CAST)) {
      if (CallExpr* move = toCallExpr(call->parentExpr)) {
        if (move->isPrimitive(PRIM_MOVE) || move->isPrimitive(PRIM_ASSIGN)) {
          if (move->get(1)->typeInfo() != call->typeInfo()) {
            SET_LINENO(call);
            VarSymbol* tmp = newTemp(call->typeInfo());
            move->insertBefore(new DefExpr(tmp));
            call->replace(new SymExpr(tmp));
            move->insertBefore(new CallExpr(PRIM_MOVE, tmp, call));
          }
        }
      }
    }
  }
}


static void derefWideStringActuals()
{
  //
  // dereference wide string actual argument to primitive
  //
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->parentSymbol && call->primitive) {
      if (call->primitive->tag == PRIM_UNKNOWN ||
          call->isPrimitive(PRIM_CAST)) {
        for_actuals(actual, call) {
          if (actual->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
            if (actual->typeInfo()->getField("addr")->typeInfo() == dtString) {
              SET_LINENO(call);
              VarSymbol* tmp = newTemp(actual->typeInfo()->getField("addr")->typeInfo());
              call->getStmtExpr()->insertBefore(new DefExpr(tmp));
              call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_DEREF, actual->copy())));
              actual->replace(new SymExpr(tmp));
            }
          }
        }
      }
    }
  }
}


static void derefWideRefsToWideClasses()
{
  //
  // dereference wide references to wide classes in select primitives;
  // this simplifies the implementation of these primitives
  //
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->isPrimitive(PRIM_GET_MEMBER) ||
        call->isPrimitive(PRIM_GET_MEMBER_VALUE) ||
        call->isPrimitive(PRIM_WIDE_GET_LOCALE) ||
        call->isPrimitive(PRIM_WIDE_GET_NODE) ||
        call->isPrimitive(PRIM_WIDE_GET_ADDR) ||
        call->isPrimitive(PRIM_SET_MEMBER)) {
      if (call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) &&
          call->get(1)->getValType()->symbol->hasFlag(FLAG_WIDE_CLASS)) {
        SET_LINENO(call);
        VarSymbol* tmp = newTemp(call->get(1)->getValType());
        call->getStmtExpr()->insertBefore(new DefExpr(tmp));
        call->getStmtExpr()->insertBefore(new CallExpr(PRIM_MOVE, tmp, new CallExpr(PRIM_DEREF, call->get(1)->remove())));
        call->insertAtHead(tmp);
      }
    }
  }
}


static void widenGetPrivClass()
{
  //
  // widen class types in certain primitives, e.g., GET_PRIV_CLASS
  //
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->isPrimitive(PRIM_GET_PRIV_CLASS)) {
      SET_LINENO(call);
      if (!call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE_CLASS))
        call->get(1)->replace(new SymExpr(wideClassMap.get(call->get(1)->typeInfo())->symbol));
      else
        call->get(1)->replace(new SymExpr(call->get(1)->typeInfo()->symbol));
    }
  }
}


// In every move:
//   if the LHS type has the WIDE or REF flag
//   and its value type is a wide class
//   and the RHS type is the same as the contents of the wide pointer:
//     Create a temp copy of the RHS, and
//     replace the RHS of the move with the temp.
static void moveAddressSourcesToTemp()
{
  forv_Vec(CallExpr, call, gCallExprs) {
    if (call->isPrimitive(PRIM_MOVE)) {
      if ((call->get(1)->typeInfo()->symbol->hasFlag(FLAG_WIDE) ||
           call->get(1)->typeInfo()->symbol->hasFlag(FLAG_REF)) &&
          call->get(1)->getValType()->symbol->hasFlag(FLAG_WIDE_CLASS) &&
          call->get(2)->typeInfo() == call->get(1)->getValType()->getField("addr")->type) {
        //
        // widen rhs class
        //
        SET_LINENO(call);
        VarSymbol* tmp = newTemp(call->get(1)->getValType());
        call->insertBefore(new DefExpr(tmp));
        call->insertBefore(new CallExpr(PRIM_MOVE, tmp, call->get(2)->remove()));
        call->insertAtTail(tmp);
      }
    }
  }
}
