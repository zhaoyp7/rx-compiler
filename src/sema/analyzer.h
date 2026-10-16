#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast/ast.h"
#include "symbols.h"
#include "types.h"

namespace rx {

enum class ConstType { I32, U32, Isize, Usize, Bool };

struct ConstVal {
  ConstType type = ConstType::I32;
  int64_t value = 0;
  bool boolean = false;
};

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

  std::unordered_map<int, ConstVal> constValues;
  std::unordered_map<int, int> constState; // 0 未访问 1 正在求值 2 完成

  TypeArena types;
  std::unordered_map<int, std::vector<TypeId>> structFieldTypes;
  std::unordered_map<int, StructLayout> structLayouts;
  std::unordered_map<int, int> layoutState; // 0 未访问 1 正在求值 2 成功 3 失败

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

  bool calcConstValue(const ConstValue &cv, ConstVal &out);
  bool calcConstValue(const ConstValue &cv, ConstType expected, ConstVal &out);
  bool calcConstLength(const ConstValue &cv);
  bool calcConstSymbol(int id, ConstVal &out);
  int resolveConstPath(const ConstPath &path);
  ConstType typeFromSyntax(const TypeSyntax &type, bool &ok);

  TypeId resolveTypeId(const TypeSyntax &type, bool &ok);
  std::string typeToString(TypeId id) const;
  bool layoutStruct(int id);
  void checkLayouts();
  bool typeSizeAlign(TypeId id, uint32_t &size, uint32_t &align);
};

} // namespace rx
