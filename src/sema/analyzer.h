#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <unordered_set>


#include "ast/ast.h"
#include "symbols.h"
#include "types.h"

namespace rx {

struct LoopContext {
  bool isWhile = false;
  bool isBarrier = false;
  TypeId expected = InvalidType;
  std::vector<TypeId> breakTypes;
};
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

  std::unordered_map<int, std::vector<TypeId>> fnParamTypes;
  std::unordered_map<int, TypeId> fnReturnTypes;
  std::unordered_map<int, TypeId> fnReceiverTypes;
  std::unordered_map<const FnDecl *, int> fnSymbols;

  std::map<int, int> derivedCloneSymbols;

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
  std::unordered_map<int, std::unordered_map<std::string, TypeId>> structFieldMap;
  std::unordered_map<int, StructLayout> structLayouts;
  std::unordered_map<int, int> layoutState; // 0 未访问 1 正在求值 2 成功 3 失败

  std::unordered_map<const Expr *, TypeId> exprTypes;

  TypeId currentReturnType = InvalidType;
  std::vector<LoopContext> loops;

  int addSymbol(const std::string &name, SymbolKind kind, SourceSpan span,
                const Node *decl);
  bool hasSelfParam(const FnDecl &fn);
  bool isIntType(TypeId t);
  bool isSignedType(TypeId t);
  bool isBoolType(TypeId t);
  TypeId stripSharedRef(TypeId t);
  bool typesCompatible(TypeId a, TypeId b);
  bool isPlace(const Expr &e);
  bool isMutablePlace(const Expr &e);
  bool isMutableAccess(const Expr &e);
  bool isCopyType(TypeId t);
  TypeId autoDerefContainers(TypeId t);
  bool isUnit(TypeId t);
  bool isNever(TypeId t);
  bool isUnitCompatible(TypeId t);
  TypeId unitType();
  TypeId neverType();
  TypeId joinTypes(TypeId a, TypeId b);
  void error();
  void pushScope();
  void popScope();

  void registerBuiltins();
  void registerBuiltinSignatures();
  void collectCrate(const Crate &crate);
  void checkEntry();

  int lookupValue(const std::string &name);
  void bindLocal(const std::string &name, int id);

  void resolveCrate(const Crate &crate);
  void resolveSignatures(const Crate &crate);
  void resolveSignature(const FnDecl &fn, int symbolId, int owner);
  void bindSelf(const FnDecl &fn, int symbolId);
  bool checkCallArgs(int symbolId, const std::vector<ExprPtr> &args, size_t startParam = 0);
  int findUserMethod(TypeId receiver, const std::string &name, TypeId &selectedReceiver);
  bool findBuiltinClone(TypeId receiver, TypeId &result);
  bool checkDerives();
  bool supportsClone(TypeId t, std::unordered_set<int> &visiting);
  bool supportsPartialEq(TypeId t, std::unordered_set<int> &visiting);
  bool supportsEq(TypeId t, std::unordered_set<int> &visiting);
  int getDerivedClone(int structId);
  void resolveFunction(const FnDecl &fn, bool inImpl);
  void resolveType(const TypeSyntax &type);
  TypeId resolveBlock(const BlockExpr &block, TypeId expected = InvalidType);
  bool resolveStmt(const Stmt &stmt);
  void resolveExpr(const Expr &expr);
  int resolvePath(const PathExpr &path);

  bool calcConstValue(const ConstValue &cv, ConstVal &out);
  bool calcConstValue(const ConstValue &cv, ConstType expected, ConstVal &out);
  bool calcConstLength(const ConstValue &cv);
  bool calcConstSymbol(int id, ConstVal &out);
  int resolveConstPath(const ConstPath &path);
  ConstType typeFromSyntax(const TypeSyntax &type, bool &ok);

  TypeId resolveTypeId(const TypeSyntax &type, bool &ok);
  TypeId infer(const Expr &expr, TypeId expected = InvalidType);
  std::string typeToString(TypeId id) const;
  bool layoutStruct(int id);
  void checkLayouts();
  bool typeSizeAlign(TypeId id, uint32_t &size, uint32_t &align);

  bool pathAllowsStoredMutRef(const Expr &e);
  bool isCoercible(TypeId from, TypeId to);
  bool checkCoercion(const Expr &expr, TypeId from, TypeId to);
  bool checkOrderingOperands(TypeId left, TypeId right);
  bool hasMutableContainerAccess(const Expr &base);
};

} // namespace rx
