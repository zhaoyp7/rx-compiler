#pragma once

#include <vector>

#include "RxParser.h"
#include "ast.h"

namespace rx {

struct BuildResult {
  Crate crate;
  bool ok = true;
};

class ASTBuilder {
public:
  BuildResult build(RxParser::CrateContext *tree);

private:
  // ---- 顶层与 item ----
  Crate buildCrate(RxParser::CrateContext *ctx);
  ItemPtr buildItem(RxParser::ItemContext *ctx);
  ItemPtr buildFunction(RxParser::FunctionDefinitionContext *ctx);
  ItemPtr buildStruct(RxParser::StructDefinitionContext *ctx);
  ItemPtr buildConstItem(RxParser::ConstantItemContext *ctx);
  ItemPtr buildImpl(RxParser::InherentImplContext *ctx);
  std::vector<Param> buildParams(RxParser::FunctionParametersContext *ctx);
  Param buildSelfParam(RxParser::SelfParamContext *ctx);
  Param buildParam(RxParser::FunctionParamContext *ctx);
  Derives
  buildDerives(const std::vector<RxParser::OuterAttributeContext *> &attrs);
  FieldDef buildFieldDef(RxParser::StructFieldContext *ctx);

  // ---- 类型 ----
  TypePtr buildType(antlr4::tree::ParseTree *node);
  TypePtr buildReferenceType(RxParser::ReferenceTypeContext *ctx);
  TypePtr buildArrayType(RxParser::ArrayTypeContext *ctx);
  TypePtr buildPathType(RxParser::TypePathContext *ctx);
  std::vector<PathSegment>
  buildTypePathSegments(RxParser::TypePathContext *ctx);

  // ---- 语句与块 ----
  BlockPtr buildBlock(RxParser::BlockExpressionContext *ctx);
  StmtPtr buildStmt(RxParser::StatementContext *ctx);
  StmtPtr buildLet(RxParser::LetStatementContext *ctx);

  // ---- 表达式（核心分发） ----
  ExprPtr buildExpr(antlr4::tree::ParseTree *node);
  ExprPtr buildAssignment(antlr4::tree::ParseTree *node);
  ExprPtr foldBinary(antlr4::tree::ParseTree *node, BinaryOp op);
  ExprPtr buildComparison(antlr4::tree::ParseTree *node);
  ExprPtr buildCast(antlr4::tree::ParseTree *node);
  ExprPtr buildUnary(antlr4::tree::ParseTree *node);
  ExprPtr buildPostfix(antlr4::tree::ParseTree *node);
  ExprPtr buildPrimary(antlr4::tree::ParseTree *node);

  // ---- 常量与路径 ----
  ConstValuePtr buildConst(antlr4::tree::ParseTree *node);
  std::vector<PathSegment>
  buildPathSegments(RxParser::PathInExpressionContext *ctx);
  std::vector<TypePtr> buildGenericArgs(RxParser::GenericArgsContext *ctx);

  // ---- 工具 ----
  void fail();
  bool ok_ = true;
};

} // namespace rx
