#include <stdlib.h>
#include "createAST.h"
#include "driver.h"
#include "expr.h"
#include "files.h"
// #include "findUnknownTypes.h"
#include "link.h"
#include "misc.h"
#include "nils.h"
#include "stmt.h"
#include "stringutil.h"
#include "symtab.h"
//#include "verifyASTType.h"
#include "yy.h"


Stmt* program;

char* yyfilename;
int yylineno;


static void ParseFile(char* filename, bool prelude = false) {
  yyfilename = filename;
  yylineno = 1;

  if (!prelude) {
    Symboltable::pushScope(SCOPE_FILE);
  }

  yyin = openInputFile(filename);
  
  if (yyin == NULL) {
    fail("Cannot read '%s'", filename);
  } else {
    program = nilStmt;
    yyparse();

    closeInputFile(yyin);
  }

  if (!prelude) {
    Symboltable::popScope();
  }
}


static bool stmtIsGlob(ILink* link) {
  Stmt* stmt = dynamic_cast<Stmt*>(link);

  if (stmt == NULL) {
    INT_FATAL("Non-Stmt found in StmtIsGlob");
  }
  if (stmt->isNull() ||
      (dynamic_cast<ExprStmt*>(stmt) != NULL) ||
      (dynamic_cast<BlockStmt*>(stmt) != NULL) ||
      (dynamic_cast<CondStmt*>(stmt) != NULL) ||
      (dynamic_cast<VarDefStmt*>(stmt) != NULL) ||
      (dynamic_cast<NoOpStmt*>(stmt) != NULL)) {
    return false;
  } else {
    return true;
  }
}


static Stmt* createInitFn(Stmt* program) {
  ILink* globstmts;
  ILink* initstmts;

  program->filter(stmtIsGlob, &globstmts, &initstmts);

  Stmt* initFunStmts = dynamic_cast<Stmt*>(initstmts);
  program = dynamic_cast<Stmt*>(globstmts);
  Stmt* initFunBody = new BlockStmt(initFunStmts ? initFunStmts 
                                                 : nilStmt);
  FnSymbol* initFun = Symboltable::defineFunction("__init", nilSymbol, 
						   dtVoid, initFunBody, true);
  FnDefStmt* initFunDef = new FnDefStmt(initFun);

  program = appendLink(program, initFunDef);

  //  program->traverse(new FindUnknownTypes());
  //  program->traverse(new PrintStmts());
  //  program->traverse(new VerifyASTType());
  /*
  Vec<Stmt*> stmts;
  getLinkElements(stmts, program);
  for (int i=0; i<stmts.n; i++) {
    stmts.e[i]->print(stdout);
  }
  */

  return program;
}


Stmt* fileToAST(char* filename, int debug) {
  static char* preludePath = NULL;

  if (preludePath == NULL) {
    initNils();
    initType(); // BLC : clean these up
    initExpr();

    preludePath = glomstrings(2, system_dir, "/AST/prelude.chpl");
    ParseFile(preludePath, true);
  }

  yydebug = debug;
  ParseFile(filename);

  program = createInitFn(program);

  return program;
}
