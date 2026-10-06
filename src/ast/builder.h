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
  ExprPtr buildAssignment(antlr4::ParserRuleContext *node);
  ExprPtr foldBinary(antlr4::ParserRuleContext *node, BinaryOp op);
  ExprPtr buildComparison(antlr4::ParserRuleContext *node);
  ExprPtr buildCast(antlr4::ParserRuleContext *node);
  ExprPtr buildUnary(antlr4::ParserRuleContext *node);
  ExprPtr buildPostfix(antlr4::ParserRuleContext *node);
  ExprPtr buildPrimary(antlr4::ParserRuleContext *node);
  ExprPtr buildNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx);
  ExprPtr buildNonBlockPrimary(RxParser::ConditionPrimaryWithoutBareBlockContext *ctx);
  ExprPtr buildExpressionWithBlock(antlr4::ParserRuleContext *ctx);
  ExprPtr buildLiteral(RxParser::LiteralExpressionContext *ctx);
  ExprPtr buildArrayExpr(RxParser::ArrayExpressionContext *ctx);
  ExprPtr buildIf(RxParser::IfExpressionContext *ctx);
  ExprPtr buildLoop(RxParser::ExpressionWithBlockContext *ctx);
  ExprPtr buildWhile(RxParser::ExpressionWithBlockContext *ctx);
  ExprPtr buildStructLit(RxParser::PathInExpressionContext *path, RxParser::StructExprFieldsContext *fields);

  // ---- 常量与路径 ----
  ConstValuePtr buildConst(antlr4::tree::ParseTree *node);
  std::vector<PathSegment>
  buildPathSegments(RxParser::PathInExpressionContext *ctx);
  std::vector<TypePtr> buildGenericArgs(RxParser::GenericArgsContext *ctx);

  // ---- 工具 ----
  void fail();
  SourceSpan spanOf(antlr4::ParserRuleContext *ctx);
  bool check_str_end(const std::string &str, const std::string &end);
  bool parseConstInt(const std::string &text, uint64_t &value, rx::IntSuffix &suffix);
  bool parseAssignOp(const std::string &text, AssignOp &op);
  bool parseComparisonOp(const std::string &text, BinaryOp &op);
  bool parseBinaryOp(const std::string &text, BinaryOp &op);
  ExprPtr applyDotSuffix(ExprPtr base, RxParser::DotSuffixContext *dot);
  ConstValuePtr buildMagnitude(RxParser::MagnitudeContext*ctx);
  PathSegment buildPathSegment(RxParser::PathIdentSegmentContext *ident, RxParser::GenericArgsContext *args);
  bool ok_ = true;
};

} // namespace rx
