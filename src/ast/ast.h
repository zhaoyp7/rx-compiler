#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rx {

struct SourceSpan {
  uint32_t begin, end;
  uint32_t line, col;
  SourceSpan() { begin = end = line = col = 0; }
};
struct AstVisitor;

struct ConstValue;
struct TypeSyntax;
struct Expr;
struct Stmt;
struct Item;
struct BlockExpr;

using ConstValuePtr = std::unique_ptr<ConstValue>;
using TypePtr = std::unique_ptr<TypeSyntax>;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using ItemPtr = std::unique_ptr<Item>;
using BlockPtr = std::unique_ptr<BlockExpr>;

struct Node {
  SourceSpan span;
  virtual ~Node() = default;
  virtual void accept(AstVisitor &) = 0;
};
enum class IntSuffix { None, I32, U32, Isize, Usize };
enum class PathSegmentKind { Ident, SelfValue, SelfType }; // x, self, Self
enum class UnaryOp { Neg, Not, Deref, Ref, RefMut };       // -, !, *, &, &mut
enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Rem,
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
  Eq,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
  LAnd,
  LOr
};
enum class AssignOp {
  Assign,
  AddAssign,
  SubAssign,
  MulAssign,
  DivAssign,
  RemAssign,
  BitAndAssign,
  BitOrAssign,
  BitXorAssign,
  ShlAssign,
  ShrAssign
};

struct PathSegment {
  std::string name;
  PathSegmentKind kind = PathSegmentKind::Ident;
  std::vector<TypePtr> genericArgs;
};
enum class ParamKind { Value, Self };
struct Param {
  ParamKind kind = ParamKind::Value;
  std::string name;
  TypePtr type;
  bool byRef = false;
  bool byMutRef = false;
  bool mutBinding = false;
};
struct FieldDef {
  std::string name;
  TypePtr type;
};
struct FieldInit {
  std::string name;
  ExprPtr value;
};
struct Derives {
  bool copy = false;
  bool clone = false;
  bool partialEq = false;
  bool eq = false;
};

struct ConstValue : Node {};
struct ConstInt : ConstValue {
  uint64_t value = 0;
  IntSuffix suffix = IntSuffix::None;
  void accept(AstVisitor &) override;
};
struct ConstBool : ConstValue {
  bool value = false;
  void accept(AstVisitor &) override;
};
struct ConstPath : ConstValue {
  std::vector<std::string> names;
  void accept(AstVisitor &) override;
};
struct ConstNeg : ConstValue {
  ConstValuePtr operand;
  void accept(AstVisitor &) override;
};

struct TypeSyntax : Node {};
struct UnitType : TypeSyntax {
  void accept(AstVisitor &) override;
};
struct ParenType : TypeSyntax {
  TypePtr inner;
  void accept(AstVisitor &) override;
};
struct PathType : TypeSyntax {
  std::vector<PathSegment> segments;
  void accept(AstVisitor &) override;
};
struct RefType : TypeSyntax {
  bool mut = false;
  TypePtr inner;
  void accept(AstVisitor &) override;
};
struct ArrayType : TypeSyntax {
  TypePtr elem;
  ConstValuePtr len;
  void accept(AstVisitor &) override;
};

struct Expr : Node {};
struct IntLit : Expr {
  uint64_t value = 0;
  IntSuffix suffix = IntSuffix::None;
  void accept(AstVisitor &) override;
};
struct BoolLit : Expr {
  bool value = false;
  void accept(AstVisitor &) override;
};
struct UnitLit : Expr {
  void accept(AstVisitor &) override;
};
struct PathExpr : Expr {
  std::vector<PathSegment> segments;
  void accept(AstVisitor &) override;
};
struct StructLit : Expr {
  std::vector<PathSegment> path;
  std::vector<FieldInit> fields;
  void accept(AstVisitor &) override;
};
struct ArrayLit : Expr {
  std::vector<ExprPtr> elements;
  void accept(AstVisitor &) override;
};
struct ArrayRepeat : Expr {
  ExprPtr elem;
  ConstValuePtr count;
  void accept(AstVisitor &) override;
};
struct CallExpr : Expr {
  ExprPtr callee;
  std::vector<ExprPtr> args;
  void accept(AstVisitor &) override;
};
struct MethodCallExpr : Expr {
  ExprPtr receiver;
  std::string method;
  std::vector<TypePtr> genericArgs;
  std::vector<ExprPtr> args;
  void accept(AstVisitor &) override;
};
struct FieldAccessExpr : Expr {
  ExprPtr receiver;
  std::string field;
  void accept(AstVisitor &) override;
};
struct IndexExpr : Expr {
  ExprPtr base;
  ExprPtr index;
  void accept(AstVisitor &) override;
};
struct UnaryExpr : Expr {
  UnaryOp op = UnaryOp::Neg;
  ExprPtr operand;
  void accept(AstVisitor &) override;
};
struct BinaryExpr : Expr {
  BinaryOp op = BinaryOp::Add;
  ExprPtr lhs;
  ExprPtr rhs;
  void accept(AstVisitor &) override;
};
struct AssignExpr : Expr {
  AssignOp op = AssignOp::Assign;
  ExprPtr lhs;
  ExprPtr rhs;
  void accept(AstVisitor &) override;
};
struct CastExpr : Expr {
  ExprPtr expr;
  TypePtr type;
  void accept(AstVisitor &) override;
};
struct IfExpr : Expr {
  ExprPtr cond;
  BlockPtr then;
  ExprPtr elseBranch;
  void accept(AstVisitor &) override;
};
struct LoopExpr : Expr {
  BlockPtr body;
  void accept(AstVisitor &) override;
};
struct WhileExpr : Expr {
  ExprPtr cond;
  BlockPtr body;
  void accept(AstVisitor &) override;
};
struct BreakExpr : Expr {
  ExprPtr value;
  void accept(AstVisitor &) override;
};
struct ContinueExpr : Expr {
  void accept(AstVisitor &) override;
};
struct ReturnExpr : Expr {
  ExprPtr value;
  void accept(AstVisitor &) override;
};
struct BlockExpr : Expr {
  std::vector<StmtPtr> stmts;
  ExprPtr tail;
  void accept(AstVisitor &) override;
};

struct Stmt : Node {};
struct LetStmt : Stmt {
  bool mut = false;
  std::string name;
  TypePtr type;
  ExprPtr init;
  void accept(AstVisitor &) override;
};
struct ExprStmt : Stmt {
  ExprPtr expr;
  void accept(AstVisitor &) override;
};

struct Item : Node {};
struct FnDecl : Item {
  std::string name;
  std::vector<Param> params;
  TypePtr ret;
  BlockPtr body;
  void accept(AstVisitor &) override;
};
struct StructDecl : Item {
  std::string name;
  Derives derives;
  std::vector<FieldDef> fields;
  void accept(AstVisitor &) override;
};
struct ConstDecl : Item {
  std::string name;
  TypePtr type;
  ConstValuePtr value;
  void accept(AstVisitor &) override;
};
struct ImplDecl : Item {
  TypePtr target;
  std::vector<ItemPtr> members;
  void accept(AstVisitor &) override;
};

struct Crate {
  std::vector<ItemPtr> items;
};
} // namespace rx