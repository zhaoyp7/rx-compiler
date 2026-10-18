#include "builder.h"
#include "RxParser.h"
#include "ast/ast.h"
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>

namespace rx {

BuildResult ASTBuilder::build(RxParser::CrateContext *tree) {
  BuildResult result;
  result.crate = buildCrate(tree);
  result.ok = ok_;
  return result;
}

Crate ASTBuilder::buildCrate(RxParser::CrateContext *ctx) {
  Crate crate;
  if (ctx == nullptr) {
    fail();
    return crate;
  }
  for (RxParser::ItemContext *item_ : ctx->item()) {
    ItemPtr item = buildItem(item_);
    if (item != nullptr) {
      crate.items.push_back(std::move(item));
    }
  }
  return crate;
}
ItemPtr ASTBuilder::buildItem(RxParser::ItemContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->useDeclaration() != nullptr) {
    return nullptr;
  } else if (ctx->functionDefinition() != nullptr) {
    return buildFunction(ctx->functionDefinition());
  } else if (ctx->structDefinition() != nullptr) {
    return buildStruct(ctx->structDefinition());
  } else if (ctx->constantItem() != nullptr) {
    return buildConstItem(ctx->constantItem());
  } else if (ctx->inherentImpl() != nullptr) {
    return buildImpl(ctx->inherentImpl());
  }
  fail();
  return nullptr;
}
ItemPtr ASTBuilder::buildFunction(RxParser::FunctionDefinitionContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto fn = std::make_unique<FnDecl>();
  fn->span = spanOf(ctx);
  fn->name = ctx->identifier()->getText();
  fn->hasGenericParams = (ctx->genericParams() != nullptr);
  if (ctx->functionParameters() != nullptr) {
    fn->params = buildParams(ctx->functionParameters());
  }
  if (ctx->typeRef() != nullptr) {
    fn->ret = buildType(ctx->typeRef());
  }
  fn->body = buildBlock(ctx->blockExpression());
  return fn;
}
std::vector<Param> ASTBuilder::buildParams(RxParser::FunctionParametersContext *ctx) {
  std::vector<Param> params;
  if (ctx->selfParam() != nullptr) {
    params.push_back(buildSelfParam(ctx->selfParam()));
  }
  for (auto *p : ctx->functionParam()) {
    params.push_back(buildParam(p));
  }
  return params;
}
Param ASTBuilder::buildSelfParam(RxParser::SelfParamContext *ctx) {
  Param p;
  p.kind = ParamKind::Self;
  p.name = "self";
  bool mut = (ctx->MUT() != nullptr);
  if (ctx->AMP() != nullptr) { // &self  &mut self
    p.byRef = true;
    p.byMutRef = mut;
  } else { // self  mut self
    p.mutBinding = mut;
  }
  return p;
}
Param ASTBuilder::buildParam(RxParser::FunctionParamContext *ctx) {
  Param p;
  p.kind = ParamKind::Value;
  auto *binding = ctx->identifierBinding();
  p.mutBinding = (binding->MUT() != nullptr);
  p.name = binding->identifier() ->getText();
  p.type = buildType(ctx->typeRef());
  return p;
}
BlockPtr ASTBuilder::buildBlock(RxParser::BlockExpressionContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto block = std::make_unique<BlockExpr>();
  auto stmts = ctx->statement();
  for (int i = 0; i < (int)stmts.size(); i++) {
    auto *tmp = stmts[i];
    if (i + 1 == stmts.size() && ctx->statementExpression() == nullptr) {
      if (tmp->SEMI() == nullptr && tmp->expressionWithBlock() != nullptr) {
        block->tail = buildExpr(tmp->expressionWithBlock());
        continue;
      }
    }
    StmtPtr stmt = buildStmt(tmp);
    if (stmt != nullptr) {
      block->stmts.push_back(std::move(stmt));
    }
  }
  if (ctx->statementExpression() != nullptr) {
    block->tail = buildExpr(ctx->statementExpression());
  }
  return block;
}
StmtPtr ASTBuilder::buildStmt(RxParser::StatementContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->letStatement() != nullptr) {
    return buildLet(ctx->letStatement());
  }
  auto stmt = std::make_unique<ExprStmt>();
  stmt->hasSemicolon = (ctx->SEMI() != nullptr);
  if (ctx->expressionWithBlock() != nullptr) {
    stmt->expr = buildExpr(ctx->expressionWithBlock());
    return stmt;
  } else if (ctx->statementExpression() != nullptr) {
    stmt->expr = buildExpr(ctx->statementExpression());
    return stmt;
  }
  return nullptr;
}
TypePtr ASTBuilder::buildType(antlr4::tree::ParseTree *node) {
  if (node == nullptr) {
    fail();
    return nullptr;
  }
  if (auto *ctx = dynamic_cast<RxParser::TypeRefContext *>(node)) {
    if (ctx->typePath() != nullptr) {
      return buildPathType(ctx->typePath());
    } else if (ctx->referenceType() != nullptr) {
      return buildReferenceType(ctx->referenceType());
    } else if (ctx->arrayType() != nullptr) {
      return buildArrayType(ctx->arrayType());
    } else if (ctx->LPAREN() != nullptr) {
      if (ctx->typeRef() == nullptr) {
        auto unit = std::make_unique<UnitType>();
        unit->span = spanOf(ctx);
        return unit;
      }
      return buildType(ctx->typeRef());
    }
  }
  if (auto *ctx = dynamic_cast<RxParser::ClosedCastTypeContext *>(node)) {
    if (ctx->LPAREN() != nullptr) {
      if (ctx->typeRef() == nullptr) {
        return std::make_unique<UnitType>();
      }
      return buildType(ctx->typeRef());
    } else if (ctx->arrayType() != nullptr) {
      return buildArrayType(ctx->arrayType());
    } else if (ctx->closedCastType() != nullptr) {
      auto innertype = buildType(ctx->closedCastType());
      if (innertype == nullptr) {
        return nullptr;
      }
      if (ctx->ANDAND() != nullptr) {
        auto outer = std::make_unique<RefType>();
        auto inner = std::make_unique<RefType>();
        outer->mut = false;
        inner->mut = (ctx->MUT() != nullptr);
        inner->inner = std::move(innertype);
        outer->inner = std::move(inner);
        return outer;
      } else {
        auto ans = std::make_unique<RefType>();
        ans->mut = (ctx->MUT() != nullptr);
        ans->inner = std::move(innertype);
        return ans;
      }
    } else if (ctx->pathIdentSegment() != nullptr) {
      auto ans = std::make_unique<PathType>();
      for (auto *tmp : ctx->typePathSegment()) {
        ans->segments.push_back(buildPathSegment(tmp->pathIdentSegment(),
                                                 tmp->genericArgs()));
      }
      ans->segments.push_back(buildPathSegment(ctx->pathIdentSegment(),
                                               ctx->genericArgs()));
      return ans;
    }
  }
  fail();
  return nullptr;
}
ConstValuePtr ASTBuilder::buildConst(antlr4::tree::ParseTree *node) {
  if (node == nullptr) {
    fail();
    return nullptr;
  }
  if (auto *ctx = dynamic_cast<RxParser::ConstValueContext *>(node)) {
    if (ctx->INTEGER_LITERAL() != nullptr) {
      auto ans = std::make_unique<ConstInt>();
      if (!parseConstInt(ctx->INTEGER_LITERAL()->getText(), ans->value, ans->suffix)) {
        fail();
        return nullptr;
      }
      return ans;
    } else if (ctx->TRUE() != nullptr) {
      auto ans = std::make_unique<ConstBool>();
      ans->value = true;
      return ans;
    } else if (ctx->FALSE() != nullptr) {
      auto ans = std::make_unique<ConstBool>();
      ans->value = false;
      return ans;
    } else if (ctx->pathInExpression() != nullptr) {
      auto ans = std::make_unique<ConstPath>();
      for (auto *tmp : ctx->pathInExpression()->pathExprSegment()) {
        ans->names.push_back(tmp->pathIdentSegment()->getText());
      }
      return ans;
    } else if (ctx->MINUS() != nullptr) {
      auto ans = std::make_unique<ConstNeg>();
      ans->operand = buildMagnitude(ctx->magnitude());
      return ans->operand ? std::move(ans) : nullptr;
    } else if (ctx->LPAREN() != nullptr && ctx->constValue() != nullptr) {
      return buildConst(ctx->constValue());
    }
  } else if (auto *ctx = dynamic_cast<RxParser::MagnitudeContext *>(node)) {
    return buildMagnitude(ctx);
  }
  fail();
  return nullptr;
}
std::vector<TypePtr> ASTBuilder::buildGenericArgs(RxParser::GenericArgsContext *ctx) {
  std::vector<TypePtr> ans;
  if (ctx == nullptr) {
    fail();
    return ans;
  }
  for (auto tmp : ctx->genericArg()) {
    if (tmp->typeRef() != nullptr) {
      ans.push_back(buildType(tmp->typeRef()));
    }
  }
  return ans;
}
std::vector<PathSegment> ASTBuilder::buildTypePathSegments(RxParser::TypePathContext *ctx) {
  std::vector<PathSegment> ans;
  if (ctx == nullptr) {
    fail();
    return ans;
  }
  for (auto tmp : ctx->typePathSegment()) {
    ans.push_back(buildPathSegment(tmp->pathIdentSegment(), tmp->genericArgs()));
  }
  return ans;
}
TypePtr ASTBuilder::buildPathType(RxParser::TypePathContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<PathType>();
  ans->segments = buildTypePathSegments(ctx);
  return ans;
}
TypePtr ASTBuilder::buildReferenceType(RxParser::ReferenceTypeContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto inner_type = buildType(ctx->typeRef());
  if (inner_type == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->ANDAND() != nullptr) { // &&T   &&mut T
    auto inner = std::make_unique<RefType>();
    auto outer = std::make_unique<RefType>();
    outer->mut = false;
    inner->mut = (ctx->MUT() != nullptr);
    inner->inner = std::move(inner_type);
    outer->inner = std::move(inner);
    return outer;
  } else { //&T   &mut T
    auto ans = std::make_unique<RefType>();
    ans->mut = (ctx->MUT() != nullptr);
    ans->inner = std::move(inner_type);
    return ans;
  }
}
TypePtr ASTBuilder::buildArrayType(RxParser::ArrayTypeContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<ArrayType>();
  ans->elem = buildType(ctx->typeRef());
  ans->len = buildConst(ctx->constValue());
  if (ans->elem == nullptr || ans->len == nullptr) {
    fail();
    return nullptr;
  }
  return ans;
}
ItemPtr ASTBuilder::buildStruct(RxParser::StructDefinitionContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<StructDecl>();
  ans->name = ctx->identifier()->getText();
  ans->derives = buildDerives(ctx->outerAttribute());
  for (auto tmp : ctx->structField()) {
    ans->fields.push_back(buildFieldDef(tmp));
  }
  return ans;  
}
ItemPtr ASTBuilder::buildConstItem(RxParser::ConstantItemContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<ConstDecl>();
  ans->type = buildType(ctx->typeRef());
  ans->name = ctx->identifier()->getText();
  ans->value = buildConst(ctx->constValue());
  return ans;
}
ItemPtr ASTBuilder::buildImpl(RxParser::InherentImplContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<ImplDecl>();
  ans->target = buildType(ctx->typeRef());
  for (auto tmp : ctx->associatedItem()) {
    if (tmp->constantItem() != nullptr) {
      ans->members.push_back(buildConstItem(tmp->constantItem()));
    } else if (tmp->functionDefinition() != nullptr) {
      ans->members.push_back(buildFunction(tmp->functionDefinition()));
    }
  }
  return ans;
}
FieldDef ASTBuilder::buildFieldDef(RxParser::StructFieldContext *ctx) {
  FieldDef ans;
  if (ctx == nullptr) {
    fail();
    return ans;
  }
  ans.name = ctx->identifier()->getText();
  ans.type = buildType(ctx->typeRef());
  return ans;
}
Derives ASTBuilder::buildDerives(const std::vector<RxParser::OuterAttributeContext *> &attrs) {
  Derives ans;
  std::unordered_map<std::string, bool> map;
  for (auto attr : attrs) {
    for (auto name : attr->deriveName()) {
      if (name->COPY() != nullptr) {
        ans.copy = true;
        ans.duplicate |= (map.find("copy") != map.end());
        map["copy"] = true;
      } else if (name->CLONE() != nullptr) {
        ans.clone = true;
        ans.duplicate |= (map.find("clone") != map.end());
        map["clone"] = true;
      } else if (name->PARTIAL_EQ() != nullptr) {
        ans.partialEq = true;
        ans.duplicate |= (map.find("partialEq") != map.end());
        map["partialEq"] = true;
      } else if (name->EQ() != nullptr) {
        ans.eq = true;
        ans.duplicate |= (map.find("eq") != map.end());
        map["eq"] = true;
      }
    }
  }
  return ans;
}
std::vector<PathSegment> ASTBuilder::buildPathSegments(RxParser::PathInExpressionContext *ctx) {
  std::vector<PathSegment> ans;
  if (ctx == nullptr) {
    fail();
    return ans;
  }
  for (auto tmp : ctx->pathExprSegment()) {
    ans.push_back(buildPathSegment(tmp->pathIdentSegment(), tmp->genericArgs()));
  }
  return ans;
}
StmtPtr ASTBuilder::buildLet(RxParser::LetStatementContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<LetStmt>();
  auto tmp = ctx->identifierBinding();
  ans->mut = (tmp->MUT() != nullptr);
  ans->name = tmp->identifier()->getText();
  if (ctx->typeRef() != nullptr) {
    ans->type = buildType(ctx->typeRef());
  }
  ans->init = buildExpr(ctx->expression());
  return ans;
}
ExprPtr ASTBuilder::buildExpr(antlr4::tree::ParseTree *node) {
  if (node == nullptr) { 
    fail(); 
    return nullptr; 
  }
  auto *ctx = dynamic_cast<antlr4::ParserRuleContext *>(node);
  if (ctx == nullptr) { 
    fail(); 
    return nullptr; 
  }

  switch (ctx->getRuleIndex()) {
  // ---------- 根透传（单子节点） ----------
  case RxParser::RuleExpression:
  case RxParser::RuleConditionExpression:
  case RxParser::RuleConditionBreakExpression:
  case RxParser::RuleStatementExpression:
    return buildExpr(ctx->children[0]);

  // ---------- 赋值（右结合） ----------
  case RxParser::RuleAssignmentExpression:
  case RxParser::RuleConditionAssignmentExpression:
  case RxParser::RuleConditionBreakAssignmentExpression:
  case RxParser::RuleStatementAssignmentExpression:
    return buildAssignment(ctx);

  // ---------- 逻辑或 ----------
  case RxParser::RuleLogicalOrExpression:
  case RxParser::RuleConditionLogicalOrExpression:
  case RxParser::RuleConditionBreakLogicalOrExpression:
  case RxParser::RuleStatementLogicalOrExpression:
    return foldBinary(ctx, BinaryOp::LOr);

  // ---------- 逻辑与 ----------
  case RxParser::RuleLogicalAndExpression:
  case RxParser::RuleConditionLogicalAndExpression:
  case RxParser::RuleConditionBreakLogicalAndExpression:
  case RxParser::RuleStatementLogicalAndExpression:
    return foldBinary(ctx, BinaryOp::LAnd);

  // ---------- 比较（单运算符） ----------
  case RxParser::RuleComparisonExpression:
  case RxParser::RuleConditionComparisonExpression:
  case RxParser::RuleConditionBreakComparisonExpression:
  case RxParser::RuleStatementComparisonExpression:
    return buildComparison(ctx);

  // ---------- 按位或（含 closed 消歧变体） ----------
  case RxParser::RuleBitOrExpression:
  case RxParser::RuleClosedBitOrExpression:
  case RxParser::RuleConditionBitOrExpression:
  case RxParser::RuleConditionClosedBitOrExpression:
  case RxParser::RuleConditionBreakBitOrExpression:
  case RxParser::RuleConditionBreakClosedBitOrExpression:
  case RxParser::RuleStatementBitOrExpression:
  case RxParser::RuleStatementClosedBitOrExpression:
    return foldBinary(ctx, BinaryOp::BitOr);

  // ---------- 异或 ----------
  case RxParser::RuleBitXorExpression:
  case RxParser::RuleClosedBitXorExpression:
  case RxParser::RuleConditionBitXorExpression:
  case RxParser::RuleConditionClosedBitXorExpression:
  case RxParser::RuleConditionBreakBitXorExpression:
  case RxParser::RuleConditionBreakClosedBitXorExpression:
  case RxParser::RuleStatementBitXorExpression:
  case RxParser::RuleStatementClosedBitXorExpression:
    return foldBinary(ctx, BinaryOp::BitXor);

  // ---------- 按位与 ----------
  case RxParser::RuleBitAndExpression:
  case RxParser::RuleClosedBitAndExpression:
  case RxParser::RuleConditionBitAndExpression:
  case RxParser::RuleConditionClosedBitAndExpression:
  case RxParser::RuleConditionBreakBitAndExpression:
  case RxParser::RuleConditionBreakClosedBitAndExpression:
  case RxParser::RuleStatementBitAndExpression:
  case RxParser::RuleStatementClosedBitAndExpression:
    return foldBinary(ctx, BinaryOp::BitAnd);

  // ---------- 移位 ----------
  case RxParser::RuleShiftExpression:
  case RxParser::RuleClosedShiftExpression:
  case RxParser::RuleConditionShiftExpression:
  case RxParser::RuleConditionClosedShiftExpression:
  case RxParser::RuleConditionBreakShiftExpression:
  case RxParser::RuleConditionBreakClosedShiftExpression:
  case RxParser::RuleStatementShiftExpression:
  case RxParser::RuleStatementClosedShiftExpression:
    return foldBinary(ctx, BinaryOp::Shl);   // 具体 Shl/Shr 由 foldBinary 内部判断

  // ---------- 加减 ----------
  case RxParser::RuleAdditiveExpression:
  case RxParser::RuleClosedAdditiveExpression:
  case RxParser::RuleConditionAdditiveExpression:
  case RxParser::RuleConditionClosedAdditiveExpression:
  case RxParser::RuleConditionBreakAdditiveExpression:
  case RxParser::RuleConditionBreakClosedAdditiveExpression:
  case RxParser::RuleStatementAdditiveExpression:
  case RxParser::RuleStatementClosedAdditiveExpression:
    return foldBinary(ctx, BinaryOp::Add);

  // ---------- 乘除模 ----------
  case RxParser::RuleMultiplicativeExpression:
  case RxParser::RuleClosedMultiplicativeExpression:
  case RxParser::RuleConditionMultiplicativeExpression:
  case RxParser::RuleConditionClosedMultiplicativeExpression:
  case RxParser::RuleConditionBreakMultiplicativeExpression:
  case RxParser::RuleConditionBreakClosedMultiplicativeExpression:
  case RxParser::RuleStatementMultiplicativeExpression:
  case RxParser::RuleStatementClosedMultiplicativeExpression:
    return foldBinary(ctx, BinaryOp::Mul);

  // ---------- 强转 ----------
  case RxParser::RuleCastExpression:
  case RxParser::RuleClosedCastExpression:
  case RxParser::RuleConditionCastExpression:
  case RxParser::RuleConditionClosedCastExpression:
  case RxParser::RuleConditionBreakCastExpression:
  case RxParser::RuleConditionBreakClosedCastExpression:
  case RxParser::RuleStatementCastExpression:
  case RxParser::RuleStatementClosedCastExpression:
    return buildCast(ctx);

  // ---------- 一元 ----------
  case RxParser::RuleUnaryExpression:
  case RxParser::RuleConditionUnaryExpression:
  case RxParser::RuleConditionBreakUnaryExpression:
  case RxParser::RuleStatementUnaryExpression:
    return buildUnary(ctx);

  // ---------- 后缀 ----------
  case RxParser::RulePostfixExpression:
  case RxParser::RuleConditionPostfixExpression:
  case RxParser::RuleConditionBreakPostfixExpression:
  case RxParser::RuleStatementPostfixExpression:
    return buildPostfix(ctx);

  // ---------- primary / 块类 ----------
  case RxParser::RulePrimaryExpression:
  case RxParser::RuleNonBlockPrimary:
  case RxParser::RuleConditionPrimary:
  case RxParser::RuleConditionPrimaryWithoutBareBlock:
  case RxParser::RuleExpressionWithBlock:
    return buildPrimary(ctx);

  default:
    fail();
    return nullptr;
  }
}
ExprPtr ASTBuilder::buildAssignment(antlr4::ParserRuleContext *node) {
  if (node == nullptr) {
    fail();
    return nullptr;
  }
  if (node->children.size() == 1) {
    return buildExpr(node->children[0]);
  } else if (node->children.size() != 3) {
    fail();
    return nullptr;
  }
  AssignOp op;
  if (!parseAssignOp(node->children[1]->getText(), op)) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<AssignExpr>();
  ans->op = op;
  ans->lhs = buildExpr(node->children[0]);
  ans->rhs = buildExpr(node->children[2]);
  if (ans->lhs == nullptr || ans->rhs == nullptr) {
    fail();
    return nullptr;
  }
  return ans;
}
ExprPtr ASTBuilder::foldBinary(antlr4::ParserRuleContext *node, BinaryOp op) {
  if (node == nullptr || node->children.size() == 0) {
    fail();
    return nullptr;
  }
  ExprPtr ans = buildExpr(node->children[0]);
  if (ans == nullptr) {
    fail();
    return nullptr;
  }
  for (int i = 1; i + 1 < (int) node->children.size(); i += 2) {
    BinaryOp op_;
    if (!parseBinaryOp(node->children[i]->getText(), op_)) {
      fail();
      return nullptr;
    }
    ExprPtr tmp = buildExpr(node->children[i + 1]);
    if (tmp == nullptr) {
      fail();
      return nullptr;
    }
    auto res = std::make_unique<BinaryExpr>();
    res->op = op_;
    res->lhs = std::move(ans);
    res->rhs = std::move(tmp);
    ans = std::move(res);
  }
  return ans;
}
ExprPtr ASTBuilder::buildComparison(antlr4::ParserRuleContext *node) {
  if (node == nullptr) {
    fail();
    return nullptr;
  }
  if (node->children.size() == 1) {
    return buildExpr(node->children[0]);
  } else if (node->children.size() != 3) {
    fail();
    return nullptr;
  }
  BinaryOp op;
  if (!parseComparisonOp(node->children[1]->getText(), op)) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<BinaryExpr>();
  ans->op = op;
  ans->lhs = buildExpr(node->children[0]);
  ans->rhs = buildExpr(node->children[2]);
  if (ans->lhs == nullptr || ans->rhs == nullptr) {
    fail();
    return nullptr;
  }
  return ans;
}
ExprPtr ASTBuilder::buildCast(antlr4::ParserRuleContext *node) {
  if (node == nullptr || node->children.empty()) {
    fail();
    return nullptr;
  }
  ExprPtr ans = buildExpr(node->children[0]);
  for (int i = 1; i + 1 < node->children.size(); i += 2) {
    TypePtr type = buildType(node->children[i + 1]);
    if (type == nullptr) {
      return nullptr;
    }
    auto tmp = std::make_unique<CastExpr>();
    tmp->expr = std::move(ans);
    tmp->type = std::move(type);
    ans = std::move(tmp);
  }
  if (ans == nullptr) {
    fail();
    return nullptr;
  }
  return ans;
}
ExprPtr ASTBuilder::buildUnary(antlr4::ParserRuleContext *node) {
  if (node == nullptr) {
    fail();
    return nullptr;
  }
  if (node->children.size() == 1) {
    return buildExpr(node->children[0]);
  } else if (node->children.size() != 2) {
    fail();
    return nullptr;
  }
  auto opptr = dynamic_cast<RxParser::UnaryOperatorContext *>(node->children[0]);
  if (opptr == nullptr) {
    fail();
    return nullptr;
  }
  auto operand = buildExpr(node->children[1]);
  if (operand == nullptr) {
    fail();
    return nullptr;
  }
  if (opptr->ANDAND() != nullptr) {
    auto inner = std::make_unique<UnaryExpr>();
    auto outer = std::make_unique<UnaryExpr>();
    inner->op = (opptr->MUT() != nullptr) ? UnaryOp::RefMut : UnaryOp::Ref;
    inner->operand = std::move(operand);
    outer->op = UnaryOp::Ref;
    outer->operand = std::move(inner);
    return outer;
  }
  auto ans = std::make_unique<UnaryExpr>();
  if (opptr->MINUS() != nullptr) {
    ans->op = UnaryOp::Neg;
  } else if (opptr->NOT() != nullptr) {
    ans->op = UnaryOp::Not;
  } else if (opptr->STAR() != nullptr) {
    ans->op = UnaryOp::Deref;
  } else if (opptr->AMP() != nullptr) {
    ans->op = (opptr->MUT() != nullptr) ? UnaryOp::RefMut : UnaryOp::Ref;
  } else {
    fail();
    return nullptr;
  }
  ans->operand = std::move(operand);
  return ans;
}
ExprPtr ASTBuilder::buildPostfix(antlr4::ParserRuleContext *node) {
  if (node == nullptr || node->children.size() == 0) {
    fail();
    return nullptr;
  }
  ExprPtr ans = buildExpr(node->children[0]);
  if (ans == nullptr) {
    fail();
    return nullptr;
  }
  for (int i = 1; i < (int) node->children.size(); i++) {
    auto child = node->children[i];
    if (auto dot = dynamic_cast<RxParser::DotSuffixContext *>(child)) {
      ans = applyDotSuffix(std::move(ans), dot);
      if (ans == nullptr) {
        fail();
        return nullptr;
      }
      continue;
    }
    auto suf = dynamic_cast<RxParser::PostfixSuffixContext *>(child);
    if (suf == nullptr) {
      fail();
      return nullptr;
    }
    if (suf->callArguments() != nullptr)  {
      auto res = std::make_unique<CallExpr>();
      res->callee = std::move(ans);
      for (auto tmp : suf->callArguments()->expression()) {
        res->args.push_back(buildExpr(tmp));
      }
      ans = std::move(res);
    } else if (suf->expression() != nullptr) {
      auto ind = buildExpr(suf->expression());
      if (ind == nullptr) {
        fail();
        return nullptr;
      }
      auto res = std::make_unique<IndexExpr>();
      res->base = std::move(ans);
      res->index = std::move(ind);
      ans = std::move(res);
    } else if (suf->dotSuffix() != nullptr) {
      ans = applyDotSuffix(std::move(ans), suf->dotSuffix());
    } else {
      fail();
      return nullptr;
    }
  }
  return ans;
}
ExprPtr ASTBuilder::buildPrimary(antlr4::ParserRuleContext *node) {
  if (node == nullptr) { 
    fail(); 
    return nullptr; 
  }
  switch (node->getRuleIndex()) {
  case RxParser::RuleExpressionWithBlock:
    return buildExpressionWithBlock(node);
  case RxParser::RulePrimaryExpression: {
    auto *ctx = dynamic_cast<RxParser::PrimaryExpressionContext *>(node);
    if (ctx->nonBlockPrimary()) {
      return buildNonBlockPrimary(ctx->nonBlockPrimary());
    } else {
      return buildExpressionWithBlock(ctx->expressionWithBlock());
    }
  }
  case RxParser::RuleConditionPrimary: {
    auto *ctx = dynamic_cast<RxParser::ConditionPrimaryContext *>(node);
    if (ctx->conditionPrimaryWithoutBareBlock()) {
      return buildNonBlockPrimary(ctx->conditionPrimaryWithoutBareBlock());
    } else {
      return buildBlock(ctx->blockExpression());
    }
  }
  case RxParser::RuleNonBlockPrimary:
    return buildNonBlockPrimary(dynamic_cast<RxParser::NonBlockPrimaryContext *>(node));
  case RxParser::RuleConditionPrimaryWithoutBareBlock:
    return buildNonBlockPrimary(dynamic_cast<RxParser::ConditionPrimaryWithoutBareBlockContext *>(node));
  default: {
    fail();
    return nullptr;
  }
  }
}
ExprPtr ASTBuilder::buildNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->literalExpression() != nullptr) {
    return buildLiteral(ctx->literalExpression());
  } else if (ctx->pathInExpression() != nullptr && ctx->LBRACE() != nullptr) {
    return buildStructLit(ctx->pathInExpression(), ctx->structExprFields());
  } else if (ctx->pathInExpression() != nullptr) {
    auto ans = std::make_unique<PathExpr>();
    ans->segments = buildPathSegments(ctx->pathInExpression());
    return ans;
  } else if (ctx->LPAREN() != nullptr && ctx->expression() == nullptr) {
    return std::make_unique<UnitLit>();
  } else if (ctx->LPAREN() != nullptr && ctx->expression() != nullptr) {
    return buildExpr(ctx->expression());
  } else if (ctx->arrayExpression() != nullptr) {
    return buildArrayExpr(ctx->arrayExpression());
  } else if (ctx->BREAK() != nullptr) {
    auto ans = std::make_unique<BreakExpr>();
    if (ctx->expression() != nullptr) {
      ans->value = buildExpr(ctx->expression());
    } else {
      ans->value = nullptr;
    }
    return ans;
  } else if (ctx->RETURN() != nullptr) {
    auto ans = std::make_unique<ReturnExpr>();
    if (ctx->expression() != nullptr) {
      ans->value = buildExpr(ctx->expression());
    } else {
      ans->value = nullptr;
    }
    return ans;
  } else if (ctx->CONTINUE() != nullptr) {
    return std::make_unique<ContinueExpr>();
  } else {
    fail();
    return nullptr;
  }
}
ExprPtr ASTBuilder::buildNonBlockPrimary(RxParser::ConditionPrimaryWithoutBareBlockContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->literalExpression() != nullptr) {
    return buildLiteral(ctx->literalExpression());
  } else if (ctx->pathInExpression() != nullptr) {
    auto ans = std::make_unique<PathExpr>();
    ans->segments = buildPathSegments(ctx->pathInExpression());
    return ans;
  } else if (ctx->LPAREN() != nullptr && ctx->expression() == nullptr) {
    return std::make_unique<UnitLit>();
  } else if (ctx->LPAREN() != nullptr && ctx->expression() != nullptr) {
    return buildExpr(ctx->expression());
  } else if (ctx->arrayExpression() != nullptr) {
    return buildArrayExpr(ctx->arrayExpression());
  } else if (ctx->BREAK() != nullptr) {
    auto ans = std::make_unique<BreakExpr>();
    if (ctx->conditionBreakExpression() != nullptr) {
      ans->value = buildExpr(ctx->conditionBreakExpression());
    } else {
      ans->value = nullptr;
    }
    return ans;
  } else if (ctx->RETURN() != nullptr) {
    auto ans = std::make_unique<ReturnExpr>();
    if (ctx->conditionExpression() != nullptr) {
      ans->value = buildExpr(ctx->conditionExpression());
    } else {
      ans->value = nullptr;
    }
    return ans;
  } else if (ctx->CONTINUE() != nullptr) {
    return std::make_unique<ContinueExpr>();
  } else if (ctx->ifExpression() != nullptr) {
    return buildIf(ctx->ifExpression());
  } else if (ctx->LOOP() != nullptr) {
    auto ans = std::make_unique<LoopExpr>();
    ans->body = buildBlock(ctx->blockExpression());
    if (ans->body == nullptr) {
      fail();
      return nullptr;
    }
    return ans;
  } else if (ctx->WHILE() != nullptr) {
    auto ans = std::make_unique<WhileExpr>();
    ans->cond = buildExpr(ctx->conditionExpression());
    ans->body = buildBlock(ctx->blockExpression());
    if (ans->cond == nullptr || ans->body == nullptr) {
      fail();
      return nullptr;
    }
    return ans;
  } else {
    fail();
    return nullptr;
  }
}
ExprPtr ASTBuilder::buildExpressionWithBlock(antlr4::ParserRuleContext *ctx) {
  auto *tmp = dynamic_cast<RxParser::ExpressionWithBlockContext *>(ctx);
  if (tmp == nullptr) {
    fail();
    return nullptr;
  }
  if (tmp->ifExpression() != nullptr) {
    return buildIf(tmp->ifExpression());
  } else if (tmp->LOOP() != nullptr) {
    return buildLoop(tmp);
  } else if (tmp->WHILE() != nullptr) {
    return buildWhile(tmp);
  } else if (tmp->blockExpression() != nullptr) {
    return buildBlock(tmp->blockExpression());
  }
  fail();
  return nullptr;
}
ExprPtr ASTBuilder::buildLiteral(RxParser::LiteralExpressionContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->INTEGER_LITERAL() != nullptr) {
    auto ans = std::make_unique<IntLit>();
    if (!parseConstInt(ctx->INTEGER_LITERAL()->getText(), ans->value, ans->suffix)) {
      fail();
      return nullptr;
    }
    return ans;
  } else if (ctx->TRUE() != nullptr) {
    auto ans = std::make_unique<BoolLit>();
    ans->value = true;
    return ans;
  } else if (ctx->FALSE() != nullptr) {
    auto ans = std::make_unique<BoolLit>();
    ans->value = false;
    return ans;
  }
  fail();
  return nullptr;
}
ExprPtr ASTBuilder::buildArrayExpr(RxParser::ArrayExpressionContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->SEMI() != nullptr) {
    auto ans = std::make_unique<ArrayRepeat>();
    ans->elem = buildExpr(ctx->expression(0));
    ans->count = buildConst(ctx->constValue());
    if (ans->elem == nullptr || ans->count == nullptr) {
      fail();
      return nullptr;
    }
    return ans;
  } else {
    auto ans = std::make_unique<ArrayLit>();
    for (auto tmp : ctx->expression()) {
      ans->elements.push_back(buildExpr(tmp));
    }
    return ans;
  }
}
ExprPtr ASTBuilder::buildIf(RxParser::IfExpressionContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<IfExpr>();
  ans->cond = buildExpr(ctx->conditionExpression());
  ans->then = buildBlock(ctx->blockExpression(0));
  if (ans->cond == nullptr || ans->then == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->ELSE() != nullptr) {
    if (ctx->ifExpression()) {
      ans->elseBranch = buildIf(ctx->ifExpression());
    } else if (ctx->blockExpression().size() == 2) {
      ans->elseBranch = buildBlock(ctx->blockExpression(1));
    } else {
      fail();
      return nullptr;
    }
  } else {
    ans->elseBranch = nullptr;
  }
  return ans;
}
ExprPtr ASTBuilder::buildLoop(RxParser::ExpressionWithBlockContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<LoopExpr>();
  ans->body = buildBlock(ctx->blockExpression());
  if (ans->body == nullptr) {
    fail();
    return nullptr;
  }
  return ans;
}
ExprPtr ASTBuilder::buildWhile(RxParser::ExpressionWithBlockContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<WhileExpr>();
  ans->cond = buildExpr(ctx->conditionExpression());
  ans->body = buildBlock(ctx->blockExpression());
  if (ans->cond == nullptr || ans->body == nullptr) {
    fail();
    return nullptr;
  }
  return ans;
}
ExprPtr ASTBuilder::buildStructLit(RxParser::PathInExpressionContext *path, RxParser::StructExprFieldsContext *fields) {
  if (path == nullptr) {
    fail();
    return nullptr;
  }
  auto ans = std::make_unique<StructLit>();
  ans->path = buildPathSegments(path);
  if (fields != nullptr) {
    for (auto tmp : fields->structExprField()) {
      FieldInit field;
      field.name = tmp->identifier()->getText();
      field.value = buildExpr(tmp->expression());
      ans->fields.push_back(std::move(field));
    }
  }
  return ans;
}

void ASTBuilder::fail() { ok_ = false; }
SourceSpan ASTBuilder::spanOf(antlr4::ParserRuleContext *ctx) {
  SourceSpan span;
  span.begin = static_cast<uint32_t>(ctx->getStart()->getStartIndex());
  span.end   = static_cast<uint32_t>(ctx->getStop()->getStopIndex());
  span.line  = static_cast<uint32_t>(ctx->getStart()->getLine());
  span.col   = static_cast<uint32_t>(ctx->getStart()->getCharPositionInLine());
  return span;
}
bool ASTBuilder::check_str_end(const std::string &str, const std::string &end) {
  if (str.size() < end.size()) {
    return false;
  }
  int len = end.size();
  int sz = str.size();
  for (int i = 0; i < len; i++) {
    if (str[sz - len + i] != end[i]) {
      return false;
    }
  }
  return true;
}
bool ASTBuilder::parseConstInt(const std::string &text, uint64_t &value, rx::IntSuffix &suffix) {
  std::string str = "";
  for (int i = 0; i < (int) text.size(); i++) {
    if (text[i] != '_') {
      str += text[i];
    }
  }
  if (str == "") {
    return false;
  }
  if (check_str_end(str, "isize")) {
    suffix = rx::IntSuffix::Isize;
    str.resize(str.size() - 5);
  } else if (check_str_end(str, "usize")) {
    suffix = rx::IntSuffix::Usize;
    str.resize(str.size() - 5);
  } else if (check_str_end(str, "i32")) {
    suffix = rx::IntSuffix::I32;
    str.resize(str.size() - 3);
  } else if (check_str_end(str, "u32")) {
    suffix = rx::IntSuffix::U32;
    str.resize(str.size() - 3);
  } else {
    suffix = rx::IntSuffix::None;
  }
  if (str.size() == 0) {
    return false;
  }
  int base = 10, st = 0;
  if (str.size() >= 2 && str[0] == '0') {
    if (str[1] == 'x' || str[1] == 'X') {
      base = 16;
      st = 2;
    } else if (str[1] == 'B' || str[1] == 'b') {
      base = 2;
      st = 2;
    } else if (str[1] == 'o' || str[1] == 'O') {
      base = 8;
      st = 2;
    }
  }
  std::string number = "";
  for (int i = st; i < (int) str.size(); i++) {
    number += str[i];
  }
  try {
    size_t len = 0;
    value = std::stoull(number, &len, base);
    return (st + len == str.size());
  } catch (...) {
    return false;
  }
}
ConstValuePtr ASTBuilder::buildMagnitude(RxParser::MagnitudeContext *ctx) {
  if (ctx == nullptr) {
    fail();
    return nullptr;
  }
  if (ctx->INTEGER_LITERAL() != nullptr) {
    auto ans = std::make_unique<ConstInt>();
    if (!parseConstInt(ctx->INTEGER_LITERAL()->getText(), ans->value, ans->suffix)) {
      fail();
      return nullptr;
    }
    return ans;
  } else if (ctx->pathInExpression() != nullptr) {
    auto ans = std::make_unique<ConstPath>();
    for (auto *tmp : ctx->pathInExpression()->pathExprSegment()) {
      ans->names.push_back(tmp->pathIdentSegment()->getText());
    }
    return ans;
  } else if (ctx->LPAREN() != nullptr && ctx->magnitude() != nullptr) {
    return buildMagnitude(ctx->magnitude());
  }
  fail();
  return nullptr;
}
bool ASTBuilder::parseAssignOp(const std::string &text, AssignOp &op) {
  if (text == "=") {
    op = AssignOp::Assign;
  } else if (text == "+=") {
    op = AssignOp::AddAssign;
  } else if (text == "-=") {
    op = AssignOp::SubAssign;
  } else if (text == "*=") {
    op = AssignOp::MulAssign;
  } else if (text == "/=") {
    op = AssignOp::DivAssign;
  } else if (text == "%=") {
    op = AssignOp::RemAssign;
  } else if (text == "&=") {
    op = AssignOp::BitAndAssign;
  } else if (text == "|=") {
    op = AssignOp::BitOrAssign;
  } else if (text == "^=") {
    op = AssignOp::BitXorAssign;
  } else if (text == "<<=") {
    op = AssignOp::ShlAssign;
  } else if (text == ">>=") {
    op = AssignOp::ShrAssign;
  } else {
    return false;
  }
  return true;
}
bool ASTBuilder::parseComparisonOp(const std::string &text, BinaryOp &op) {
  if (text == "==") {
    op = BinaryOp::Eq;
  } else if (text == "!=") {
    op = BinaryOp::Ne;
  } else if (text == "<") {
    op = BinaryOp::Lt;
  } else if (text == "<=") {
    op = BinaryOp::Le;
  } else if (text == ">") {
    op = BinaryOp::Gt;
  } else if (text == ">=") {
    op = BinaryOp::Ge;
  } else {
    return false;
  }
  return true;
}
bool ASTBuilder::parseBinaryOp(const std::string &text, BinaryOp &op) {
  if (text == "+") {
    op = BinaryOp::Add;
  } else if (text == "-") {
    op = BinaryOp::Sub;
  } else if (text == "*") {
    op = BinaryOp::Mul;
  } else if (text == "/") {
    op = BinaryOp::Div;
  } else if (text == "%") {
    op = BinaryOp::Rem;
  } else if (text == "&") {
    op = BinaryOp::BitAnd;
  } else if (text == "|") {
    op = BinaryOp::BitOr;
  } else if (text == "^") {
    op = BinaryOp::BitXor;
  } else if (text == "<<") {
    op = BinaryOp::Shl;
  } else if (text == ">>") {
    op = BinaryOp::Shr;
  } else if (text == "==") {
    op = BinaryOp::Eq;
  } else if (text == "!=") {
    op = BinaryOp::Ne;
  } else if (text == "<") {
    op = BinaryOp::Lt;
  } else if (text == "<=") {
    op = BinaryOp::Le;
  } else if (text == ">") {
    op = BinaryOp::Gt;
  } else if (text == ">=") {
    op = BinaryOp::Ge;
  } else if (text == "&&") {
    op = BinaryOp::LAnd;
  } else if (text == "||") {
    op = BinaryOp::LOr;
  } else {
    return false;
  }
  return true;
}
PathSegment ASTBuilder::buildPathSegment(RxParser::PathIdentSegmentContext *ident, RxParser::GenericArgsContext *args) {
  PathSegment seg;
  if (ident == nullptr) {
    fail();
    return seg;
  }
  if (ident->identifier() != nullptr) {
    seg.name = ident->identifier()->getText();
    seg.kind = PathSegmentKind::Ident;
  } else if (ident->SELF_VALUE() != nullptr) {
    seg.name = "self";
    seg.kind = PathSegmentKind::SelfValue;
  } else {
    seg.name = "Self";
    seg.kind = PathSegmentKind::SelfType;
  }
  if (args != nullptr) {
    seg.genericArgs = buildGenericArgs(args);
  }
  return seg;
}
ExprPtr ASTBuilder::applyDotSuffix(ExprPtr base, RxParser::DotSuffixContext *dot) {
  if (dot->callArguments() != nullptr) {
    auto expr = std::make_unique<MethodCallExpr>();
    expr->receiver = std::move(base);
    auto *seg = dot->pathExprSegment();
    expr->method = seg->pathIdentSegment()->getText();
    if (seg->genericArgs()) {
      expr->genericArgs = buildGenericArgs(seg->genericArgs());
    }
    for (auto *arg : dot->callArguments()->expression()) {
      expr->args.push_back(buildExpr(arg));
    }
    return expr;
  }
  if (dot->identifier() != nullptr) {
    auto expr = std::make_unique<FieldAccessExpr>();
    expr->receiver = std::move(base);
    expr->field = dot->identifier()->getText();
    return expr;
  }
  fail();
  return nullptr;
}

} // namespace rx
