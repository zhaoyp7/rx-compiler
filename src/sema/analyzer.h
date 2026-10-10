#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ast/ast.h"
#include "symbols.h"

namespace rx {

class SemanticAnalyzer {
public:
  bool run(const Crate &crate);

private:
  std::vector<Symbol> symbols;
  std::unordered_map<std::string, int> typeTable;
  std::unordered_map<std::string, int> valueTable;
  std::unordered_map<int, std::unordered_map<std::string, int>> assocTable;

  std::vector<Scope> scopes;
  std::unordered_map<const Node *, int> bindings;
  std::unordered_map<const ImplDecl *, int> implTargets;

  int currentStruct = InvalidSymbol;
  bool inReceiverMethod = false;
  bool failed = false;

  int addSymbol(const std::string &name, SymbolKind kind, SourceSpan span,
                const Node *decl);
  bool hasSelfParam(const FnDecl &fn);
  void error();
  void pushScope();
  void popScope();

  void registerBuiltins();
  void collectCrate(const Crate &crate);
  void checkEntry();

  int lookupValue(const std::string &name);
  void bindLocal(const std::string &name, int id);

  void resolveCrate(const Crate &crate);
  void resolveFunction(const FnDecl &fn, bool inImpl);
  void resolveType(const TypeSyntax &type);
  void resolveBlock(const BlockExpr &block);
  void resolveStmt(const Stmt &stmt);
  void resolveExpr(const Expr &expr);
  int resolvePath(const PathExpr &path);
};

} // namespace rx
