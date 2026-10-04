/*
 * DumpVisitor：AST 打印器（调试工具，不参与编译结果）
 *
 * 作用：遍历 rx::Crate 或任意 AST 节点，按缩进文本输出到指定 ostream，
 *       用于人工核对 ASTBuilder 的构建结果，例如：
 *
 *         Crate
 *           FnDecl add
 *             Param a: i32
 *             Param b: i32
 *             ret: i32
 *             Block
 *               tail:
 *                 Binary +
 *                   PathExpr a
 *                   PathExpr b
 *
 * 用法：
 *   rx::DumpVisitor dumper(std::cout);
 *   dumper.dump(result.crate);          // 整个 crate
 *   someNode->accept(dumper);           // 或者单独打印某个节点
 *
 * 实现：继承 rx::AstVisitor，对 37 个具体节点分别打印节点名与关键字段
 *       （名字、运算符、字面量、类型等），并递归访问子节点；
 *       节点自身的 accept() 负责双分派。
 */
#pragma once

#include <ostream>
#include <string>

#include "visitor.h"

namespace rx {

class DumpVisitor : public AstVisitor {
public:
  explicit DumpVisitor(std::ostream &out) : out_(out) {}

  void dump(const Crate &crate);

  void visit(ConstInt &) override;
  void visit(ConstBool &) override;
  void visit(ConstPath &) override;
  void visit(ConstNeg &) override;

  void visit(UnitType &) override;
  void visit(ParenType &) override;
  void visit(PathType &) override;
  void visit(RefType &) override;
  void visit(ArrayType &) override;

  void visit(IntLit &) override;
  void visit(BoolLit &) override;
  void visit(UnitLit &) override;
  void visit(PathExpr &) override;
  void visit(StructLit &) override;
  void visit(ArrayLit &) override;
  void visit(ArrayRepeat &) override;
  void visit(CallExpr &) override;
  void visit(MethodCallExpr &) override;
  void visit(FieldAccessExpr &) override;
  void visit(IndexExpr &) override;
  void visit(UnaryExpr &) override;
  void visit(BinaryExpr &) override;
  void visit(AssignExpr &) override;
  void visit(CastExpr &) override;
  void visit(IfExpr &) override;
  void visit(LoopExpr &) override;
  void visit(WhileExpr &) override;
  void visit(BreakExpr &) override;
  void visit(ContinueExpr &) override;
  void visit(ReturnExpr &) override;
  void visit(BlockExpr &) override;

  void visit(LetStmt &) override;
  void visit(ExprStmt &) override;

  void visit(FnDecl &) override;
  void visit(StructDecl &) override;
  void visit(ConstDecl &) override;
  void visit(ImplDecl &) override;

private:
  void line(const std::string &text);
  void child(Node &node);
  void child(Node *node);
  static std::string typeName(TypeSyntax &type);
  static std::string constName(ConstValue &value);
  static std::string pathName(const std::vector<PathSegment> &segments);
  static std::string suffixName(IntSuffix suffix);

  std::ostream &out_;
  int indent_ = 0;
};

} // namespace rx
