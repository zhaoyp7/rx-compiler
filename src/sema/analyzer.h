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
  bool failed = false;

  std::unordered_map<int, ConstVal> constValues;
  std::unordered_map<int, int> constState;

  TypeArena types;
  std::unordered_map<int, std::vector<TypeId>> structFieldTypes;
  std::unordered_map<int, std::unordered_map<std::string, TypeId>> structFieldMap;
  std::unordered_map<int, StructLayout> structLayouts;
  std::unordered_map<int, int> layoutState;
  std::unordered_map<const Expr *, TypeId> exprTypes;

  TypeId currentReturnType = InvalidType;
  std::vector<LoopContext> loops;

  int addSymbol(const std::string &name, SymbolKind kind, SourceSpan span,
                const Node *decl);
  void error();
  void pushScope();
  void popScope();
  bool hasSelfParam(const FnDecl &fn);

  void registerBuiltins();
  void registerBuiltinSignatures();
  void collectCrate(const Crate &crate);
  void checkEntry();
  int lookupValue(const std::string &name);
  void bindLocal(const std::string &name, int id);

  void resolveCrate(const Crate &crate);
  void resolveConstTypes(const Crate &crate);
  void bindConstType(const ConstDecl &cd, int symbolId);
  void resolveSignatures(const Crate &crate);
  void resolveSignature(const FnDecl &fn, int symbolId, int owner);
  void bindSelf(const FnDecl &fn, int symbolId);

  bool calcConstValue(const ConstValue &cv, ConstVal &out);
  bool calcConstValue(const ConstValue &cv, ConstType expected, ConstVal &out);
  bool calcConstLength(const ConstValue &cv);
  bool calcConstSymbol(int id, ConstVal &out);
  int resolveConstPath(const ConstPath &path);
  ConstType typeFromSyntax(const TypeSyntax &type, bool &ok);

  TypeId resolveTypeId(const TypeSyntax &type, bool &ok);
  bool isIntType(TypeId t);
  bool isSignedType(TypeId t);
  bool isBoolType(TypeId t);
  bool isUnit(TypeId t);
  bool isNever(TypeId t);
  bool isUnitCompatible(TypeId t);
  TypeId unitType();
  TypeId neverType();
  TypeId stripSharedRef(TypeId t);
  TypeId joinTypes(TypeId a, TypeId b);

  bool isPlace(const Expr &e);
  bool typeAllowsMutableAccess(TypeId t, bool ownerMutable);
  bool typeHasSharedBarrier(TypeId t);
  bool blocksStoredMutRef(const Expr &e);
  bool isMutablePlace(const Expr &e);
  bool isMutableAccess(const Expr &e);
  TypeId autoDerefContainers(TypeId t);
  bool isCopyType(TypeId t);
  bool isCopyType(TypeId t, std::unordered_set<int> &visiting);

  bool typeSizeAlign(TypeId id, uint32_t &size, uint32_t &align);
  bool layoutStruct(int id);
  void checkLayouts();

  bool isCoercible(TypeId from, TypeId to);
  bool checkCoercion(const Expr &expr, TypeId from, TypeId to);
  bool checkOrderingOperands(TypeId left, TypeId right);

  bool checkCallArgs(int symbolId, const std::vector<ExprPtr> &args,
                     size_t startParam = 0);
  void collectMethodCandidates(TypeId receiver, std::vector<TypeId> &candidates);
  int findUserMethodAtCandidate(TypeId candidate, const std::string &name,
                                TypeId &selectedReceiver);
  int findUserMethod(TypeId receiver, const std::string &name,
                     TypeId &selectedReceiver);
  bool findBuiltinCloneAtCandidate(TypeId candidate, TypeId &result);
  int findCloneMethod(TypeId receiver, TypeId &selectedReceiver,
                      TypeId &result);

  bool checkDerives();
  bool supportsClone(TypeId id, std::unordered_set<int> &visiting);
  bool supportsPartialEq(TypeId id, std::unordered_set<int> &visiting);
  bool supportsEq(TypeId id, std::unordered_set<int> &visiting);
  int getDerivedClone(int structId);

  int resolvePath(const PathExpr &path);
  bool resolveStmt(const Stmt &stmt);
  TypeId resolveBlock(const BlockExpr &block, TypeId expected = InvalidType);
  void resolveFunction(const FnDecl &fn, bool inImpl);

  TypeId infer(const Expr &expr, TypeId expected = InvalidType);
  TypeId inferIntLit(const IntLit &lit, TypeId expected);
  TypeId inferBoolLit();
  TypeId inferUnitLit();
  TypeId inferPath(const PathExpr &path);
  TypeId inferCast(const CastExpr &cast);
  TypeId inferIf(const IfExpr &ifex, TypeId expected);
  TypeId inferLoop(const LoopExpr &loop, TypeId expected);
  TypeId inferWhile(const WhileExpr &whileex);
  TypeId inferBreak(const BreakExpr &breakex);
  TypeId inferContinue(const ContinueExpr &continueex);
  TypeId inferReturn(const ReturnExpr &returnex);
  TypeId inferBinary(const BinaryExpr &binary);
  TypeId inferAssign(const AssignExpr &assign);
  TypeId inferUnary(const UnaryExpr &unary);
  TypeId inferIndex(const IndexExpr &ix);
  TypeId inferCall(const CallExpr &call);
  bool tryInferBuiltinCall(const CallExpr &call, const PathExpr &path,
                           TypeId &result);
  TypeId inferArrayLit(const ArrayLit &array, TypeId expected);
  TypeId inferArrayRepeat(const ArrayRepeat &array, TypeId expected);
  TypeId inferFieldAccess(const FieldAccessExpr &field);
  TypeId inferMethodCall(const MethodCallExpr &method);
  bool tryInferBuiltinMethodCall(const MethodCallExpr &method, TypeId receiver,
                                 TypeId &result);
  TypeId inferStructLit(const StructLit &stru);
};

} // namespace rx
