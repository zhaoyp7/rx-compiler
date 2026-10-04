#pragma once
#include "ast.h"

namespace rx {

struct AstVisitor {
  virtual ~AstVisitor() = default;

  // ConstValue
  virtual void visit(ConstInt &) = 0;
  virtual void visit(ConstBool &) = 0;
  virtual void visit(ConstPath &) = 0;
  virtual void visit(ConstNeg &) = 0;

  // TypeSyntax
  virtual void visit(UnitType &) = 0;
  virtual void visit(ParenType &) = 0;
  virtual void visit(PathType &) = 0;
  virtual void visit(RefType &) = 0;
  virtual void visit(ArrayType &) = 0;

  // Expr
  virtual void visit(IntLit &) = 0;
  virtual void visit(BoolLit &) = 0;
  virtual void visit(UnitLit &) = 0;
  virtual void visit(PathExpr &) = 0;
  virtual void visit(StructLit &) = 0;
  virtual void visit(ArrayLit &) = 0;
  virtual void visit(ArrayRepeat &) = 0;
  virtual void visit(CallExpr &) = 0;
  virtual void visit(MethodCallExpr &) = 0;
  virtual void visit(FieldAccessExpr &) = 0;
  virtual void visit(IndexExpr &) = 0;
  virtual void visit(UnaryExpr &) = 0;
  virtual void visit(BinaryExpr &) = 0;
  virtual void visit(AssignExpr &) = 0;
  virtual void visit(CastExpr &) = 0;
  virtual void visit(IfExpr &) = 0;
  virtual void visit(LoopExpr &) = 0;
  virtual void visit(WhileExpr &) = 0;
  virtual void visit(BreakExpr &) = 0;
  virtual void visit(ContinueExpr &) = 0;
  virtual void visit(ReturnExpr &) = 0;
  virtual void visit(BlockExpr &) = 0;

  // Stmt
  virtual void visit(LetStmt &) = 0;
  virtual void visit(ExprStmt &) = 0;

  // Item
  virtual void visit(FnDecl &) = 0;
  virtual void visit(StructDecl &) = 0;
  virtual void visit(ConstDecl &) = 0;
  virtual void visit(ImplDecl &) = 0;
};

} // namespace rx