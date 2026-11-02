#include "ir/generator.h"

#include <iostream>
#include <ostream>
#include <string>
#include <unordered_map>

#include "ast/ast.h"
#include "ir/builder.h"
#include "ir/ir.h"
#include "ir/type.h"
#include "sema/analyzer.h"
#include "sema/types.h"

namespace rx {

IrGenerator::IrGenerator(const Crate &crate, const SemanticAnalyzer &analyzer)
    : crate(crate), analyzer(analyzer) {}
namespace {

using ir::Instruction;
using Kind = Instruction::Kind;

struct LoopContext {
  std::string breakLabel;
  std::string continueLabel;
  std::string resultSlot;
  TypeId resultType = InvalidType;
  bool isWhile = false;
};
struct LowerContext {
  ir::IrBuilder &builder;
  const ir::TypeMapper &types;
  const SemanticAnalyzer &analyzer;
  const std::string &returnType;
  std::unordered_map<int, std::string> &paramValues;
  std::vector<LoopContext> loops;

  std::string valueType(TypeId id) const { return types.valueType(id); }
  std::string storageType(TypeId id) const { return types.storageType(id); }
  TypeId exprType(const Expr &expr) const { return analyzer.exprType(expr); }
};

bool isComparison(BinaryOp op) {
  if (op == BinaryOp::Eq || op == BinaryOp::Ne) {
    return true;
  } else if (op == BinaryOp::Lt || op == BinaryOp::Le) {
    return true;
  } else if (op == BinaryOp::Gt || op == BinaryOp::Ge) {
    return true;
  }
  return false;
}
bool isSignedKind(TypeKind kind) {
  return (kind == TypeKind::I32 || kind == TypeKind::Isize);
}
bool isScalarInt(TypeId id, const SemanticAnalyzer &analyzer) {
  if (id == InvalidType) {
    return false;
  }
  TypeKind kind = analyzer.typeArena().get(id).kind;
  if (kind == TypeKind::I32 || kind == TypeKind::U32) {
    return true;
  } else if (kind == TypeKind::Isize || kind == TypeKind::Usize) {
    return true;
  }
  return false;
}
bool isUnitType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (id != InvalidType &&
          analyzer.typeArena().get(id).kind == TypeKind::Unit);
}
bool isScalarIntOrNever(TypeId id, const SemanticAnalyzer &analyzer) {
  if (id == InvalidType) {
    return false;
  }
  TypeKind kind = analyzer.typeArena().get(id).kind;
  return kind == TypeKind::Never || isScalarInt(id, analyzer);
}
bool isScalarBool(TypeId id, const SemanticAnalyzer &analyzer) {
  return (id != InvalidType &&
         analyzer.typeArena().get(id).kind == TypeKind::Bool);
}
bool isScalarValue(TypeId id, const SemanticAnalyzer &analyzer) {
  return (isScalarInt(id, analyzer) || isScalarBool(id, analyzer));
}
bool isScalarValueOrNever(TypeId id, const SemanticAnalyzer &analyzer) {
  return isScalarValue(id, analyzer) ||
         (id != InvalidType &&
          analyzer.typeArena().get(id).kind == TypeKind::Never);
}
std::string callTargetName(const Symbol &sym,
                           const SemanticAnalyzer &analyzer) {
  if (sym.kind == SymbolKind::BuiltinFn) {
    return sym.name;
  } else if (sym.kind == SymbolKind::Fn) {
    if (sym.name == "main") {
      return "_rx_main";
    }
    return "_rx_fn_" + sym.name;
  } else if (sym.kind == SymbolKind::AssocFn && sym.owner != InvalidSymbol) {
    const Symbol *owner = analyzer.symbol(sym.owner);
    if (owner == nullptr) {
      return "";
    }
    return "_rx_m_" + owner->name + "_" + sym.name;
  }
  return "";
}
ir::LowerResult failResult() { return ir::LowerResult{}; }
ir::LowerResult unitResult() { return ir::LowerResult{true}; }
ir::LowerResult terminatedResult() { return ir::LowerResult{true, true}; }
ir::LowerResult valueResult(const std::string &value) {
  return ir::LowerResult{true, false, true, value};
}

ir::LowerResult lowerIf(const IfExpr &ifex, LowerContext &ctx);
ir::LowerResult lowerWhile(const WhileExpr &whileex, LowerContext &ctx);
ir::LowerResult lowerLoop(const LoopExpr &loop, LowerContext &ctx);
ir::LowerResult lowerBreak(const BreakExpr &brk, LowerContext &ctx);
ir::LowerResult lowerContinue(const ContinueExpr &cont, LowerContext &ctx);
ir::LowerResult lowerBlock(const BlockExpr &block, LowerContext &ctx);
ir::LowerResult lowerReturn(const ReturnExpr &ret, LowerContext &ctx);
ir::LowerResult lowerLazyBool(const BinaryExpr &expr, LowerContext &ctx);
ir::LowerResult lowerCall(const CallExpr &call, LowerContext &ctx);
bool supportsBlock(const BlockExpr &block, const SemanticAnalyzer &analyzer);

ir::LowerResult lowerExpr(const Expr &expr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  const std::unordered_map<int, std::string> &paramValues = ctx.paramValues;
  const std::string &returnType = ctx.returnType;

  if (dynamic_cast<const UnitLit *>(&expr)) {
    return unitResult();
  } else if (auto *lit = dynamic_cast<const IntLit *>(&expr)) {
    return valueResult(std::to_string(lit->value));
  } else if (auto *path = dynamic_cast<const PathExpr *>(&expr)) {
    if (path->segments.size() == 1) {
      int id = analyzer.bindingOf(*path);
      if (id == InvalidSymbol) {
        return failResult();
      }
      auto it = paramValues.find(id);
      if (it != paramValues.end()) {
        return valueResult(it->second);
      }
      std::string slot = builder.getSlot(id);
      if (slot.empty()) {
        return failResult();
      }
      TypeId type = analyzer.exprType(expr);
      std::string storage = types.storageType(type);
      std::string value = builder.load(storage, slot);

      if (storage == "i8" && types.valueType(type) == "i1") {
        value = builder.cast("trunc", "i8", "i1", value);
      }
      return valueResult(value);
    }
    return failResult();
  } else if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    return lowerCall(*call, ctx);
  } else if (auto *block = dynamic_cast<const BlockExpr *>(&expr)) {
    return lowerBlock(*block, ctx);
  } else if (auto *ret = dynamic_cast<const ReturnExpr *>(&expr)) {
    return lowerReturn(*ret, ctx);
  } else if (auto *ifex = dynamic_cast<const IfExpr *>(&expr)) {
    return lowerIf(*ifex, ctx);
  } else if (auto *whileex = dynamic_cast<const WhileExpr *>(&expr)) {
    return lowerWhile(*whileex, ctx);
  } else if (auto *loop = dynamic_cast<const LoopExpr *>(&expr)) {
    return lowerLoop(*loop, ctx);
  } else if (auto *brk = dynamic_cast<const BreakExpr *>(&expr)) {
    return lowerBreak(*brk, ctx);
  } else if (auto *cont = dynamic_cast<const ContinueExpr *>(&expr)) {
    return lowerContinue(*cont, ctx);
  } else if (auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    BinaryOp op = binary->op;
    if (op == BinaryOp::LAnd || op == BinaryOp::LOr) {
      return lowerLazyBool(*binary, ctx);
    }
    auto lhs = lowerExpr(*binary->lhs, ctx);
    if (!lhs.ok) {
      return failResult();
    } else if (lhs.terminated) {
      return lhs;
    } else if (!lhs.hasValue) {
      return failResult();
    }
    auto rhs = lowerExpr(*binary->rhs, ctx);
    if (!rhs.ok) {
      return failResult();
    } else if (rhs.terminated) {
      return rhs;
    } else if (!rhs.hasValue) {
      return failResult();
    }
    TypeId operandType = analyzer.exprType(*binary->lhs);
    TypeId resultType = analyzer.exprType(expr);
    if (operandType == InvalidType || resultType == InvalidType) {
      return failResult();
    }
    const TypeInfo &operandInfo = analyzer.typeArena().get(operandType);
    bool signedOp = isSignedKind(operandInfo.kind);
    std::string operandIr = types.valueType(operandType);
    std::string resultIr = types.valueType(resultType);
    std::string opcode = "";
    if (op == BinaryOp::Eq) {
      opcode = "eq";
    } else if (op == BinaryOp::Ne) {
      opcode = "ne";
    } else if (op == BinaryOp::Lt) {
      opcode = signedOp ? "slt" : "ult";
    } else if (op == BinaryOp::Le) {
      opcode = signedOp ? "sle" : "ule";
    } else if (op == BinaryOp::Gt) {
      opcode = signedOp ? "sgt" : "ugt";
    } else if (op == BinaryOp::Ge) {
      opcode = signedOp ? "sge" : "uge";
    } else if (op == BinaryOp::Add) {
      opcode = "add";
    } else if (op == BinaryOp::Sub) {
      opcode = "sub";
    } else if (op == BinaryOp::Mul) {
      opcode = "mul";
    } else if (op == BinaryOp::Div) {
      opcode = signedOp ? "sdiv" : "udiv";
    } else if (op == BinaryOp::Rem) {
      opcode = signedOp ? "srem" : "urem";
    } else if (op == BinaryOp::BitAnd) {
      opcode = "and";
    } else if (op == BinaryOp::BitOr) {
      opcode = "or";
    } else if (op == BinaryOp::BitXor) {
      opcode = "xor";
    } else if (op == BinaryOp::Shl) {
      opcode = "shl";
    } else if (op == BinaryOp::Shr) {
      opcode = signedOp ? "ashr" : "lshr";
    }
    if (isComparison(op)) {
      return valueResult(builder.icmp(opcode, operandIr, lhs.value, rhs.value));
    }
    std::string actualRhs = rhs.value;
    if (op == BinaryOp::Shl || op == BinaryOp::Shr) {
      actualRhs = builder.binary("and", "i32", rhs.value, "31");
    }
    return valueResult(builder.binary(opcode, resultIr, lhs.value, actualRhs));
  } else if (auto *lit = dynamic_cast<const BoolLit *>(&expr)) {
    return valueResult(lit->value ? "true" : "false");
  } else if (auto *cast = dynamic_cast<const CastExpr *>(&expr)) {
    auto value = lowerExpr(*cast->expr, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    TypeId from = analyzer.exprType(*cast->expr);
    TypeId to = analyzer.exprType(expr);
    std::string fromIr = types.valueType(from);
    std::string toIr = types.valueType(to);
    if (fromIr.empty() || toIr.empty()) {
      return failResult();
    } else if (fromIr == toIr) {
      return value;
    } else if (fromIr == "i1" && toIr == "i32") {
      return valueResult(builder.cast("zext", "i1", "i32", value.value));
    }
    return failResult();
  } else if (auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    if (unary->op != UnaryOp::Neg && unary->op != UnaryOp::Not) {
      return failResult();
    }
    auto value = lowerExpr(*unary->operand, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    TypeId type = analyzer.exprType(expr);
    std::string irType = types.valueType(type);
    if (irType.empty()) {
      return failResult();
    }
    if (unary->op == UnaryOp::Neg) {
      if (irType != "i32") {
        return failResult();
      }
      return valueResult(builder.binary("sub", "i32", "0", value.value));
    } else if (unary->op == UnaryOp::Not) {
      if (irType == "i1") {
        return valueResult(builder.binary("xor", "i1", value.value, "true"));
      } else if (irType == "i32") {
        return valueResult(builder.binary("xor", "i32", value.value, "-1"));
      }
    }

    return failResult();
  }
  return failResult();
}
ir::LowerResult lowerPlace(const Expr &expr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  auto *path = dynamic_cast<const PathExpr *>(&expr);
  if (path == nullptr || path->segments.size() != 1) {
    return failResult();
  }
  int id = analyzer.bindingOf(*path);
  if (id == InvalidSymbol) {
    return failResult();
  }
  std::string slot = builder.getSlot(id);
  if (slot.empty()) {
    return failResult();
  }
  return valueResult(slot);
}
ir::LowerResult lowerAssign(const AssignExpr &assign, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  auto place = lowerPlace(*assign.lhs, ctx);
  TypeId lhsType = analyzer.exprType(*assign.lhs);
  if (!place.ok || !place.hasValue || !isScalarValue(lhsType, analyzer)) {
    return failResult();
  }
  auto rhs = lowerExpr(*assign.rhs, ctx);
  if (!rhs.ok) {
    return failResult();
  } else if (rhs.terminated) {
    return rhs;
  } else if (!rhs.hasValue) {
    return failResult();
  }
  std::string storage = types.storageType(lhsType);
  if (assign.op == AssignOp::Assign) {
    std::string stored = rhs.value;
    if (storage == "i8" && types.valueType(lhsType) == "i1") {
      stored = builder.cast("zext", "i1", "i8", rhs.value);
    }
    builder.store(storage, stored, place.value);
    return unitResult();
  }
  std::string old = builder.load(storage, place.value);
  std::string opcode;
  bool shift = false;
  bool signedl = isSignedKind(analyzer.typeArena().get(lhsType).kind);
  if (assign.op == AssignOp::AddAssign) {
    opcode = "add";
  } else if (assign.op == AssignOp::SubAssign) {
    opcode = "sub";
  } else if (assign.op == AssignOp::MulAssign) {
    opcode = "mul";
  } else if (assign.op == AssignOp::DivAssign) {
    opcode = signedl ? "sdiv" : "udiv";
  } else if (assign.op == AssignOp::RemAssign) {
    opcode = signedl ? "srem" : "urem";
  } else if (assign.op == AssignOp::BitAndAssign) {
    opcode = "and";
  } else if (assign.op == AssignOp::BitOrAssign) {
    opcode = "or";
  } else if (assign.op == AssignOp::BitXorAssign) {
    opcode = "xor";
  } else if (assign.op == AssignOp::ShlAssign) {
    opcode = "shl";
    shift = true;
  } else if (assign.op == AssignOp::ShrAssign) {
    opcode = signedl ? "ashr" : "lshr";
    shift = true;
  } else {
    return failResult();
  }
  std::string actualRhs = rhs.value;
  if (shift) {
    actualRhs = builder.binary("and", "i32", rhs.value, "31");
  }
  std::string result = builder.binary(opcode, storage, old, actualRhs);
  builder.store(storage, result, place.value);
  return unitResult();
}
ir::LowerResult lowerReturn(const ReturnExpr &ret, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const std::string &returnType = ctx.returnType;

  if (ret.value == nullptr) {
    builder.retVoid();
    return terminatedResult();
  }
  auto ans = lowerExpr(*ret.value, ctx);
  if (!ans.ok) {
    return failResult();
  }
  if (ans.hasValue) {
    if (returnType == "void") {
      return failResult();
    }
    builder.ret(returnType, ans.value);
  } else {
    if (returnType != "void") {
      return failResult();
    }
    builder.retVoid();
  }
  return terminatedResult();
}
ir::LowerResult lowerStmt(const Stmt &stmt, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (auto *let = dynamic_cast<const LetStmt *>(&stmt)) {
    if (let->init == nullptr) {
      return failResult();
    }
    int id = analyzer.symbolIdOfDecl(*let);
    if (id == InvalidSymbol) {
      return failResult();
    }
    const Symbol *sym = analyzer.symbol(id);
    if (sym == nullptr || sym->type == InvalidType) {
      return failResult();
    }
    auto value = lowerExpr(*let->init, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    std::string storage = types.storageType(sym->type);
    std::string slot = builder.ensureSlot(id, let->name, storage);
    std::string stored = value.value;
    if (storage == "i8" && types.valueType(sym->type) == "i1") {
      stored = builder.cast("zext", "i1", "i8", value.value);
    }
    builder.store(storage, stored, slot);
    return unitResult();
  } else if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    if (auto *block = dynamic_cast<const BlockExpr *>(expr->expr.get())) {
      auto result = lowerBlock(*block, ctx);
      if (!result.ok) {
        return failResult();
      } else if (result.terminated) {
        return result;
      }
      return unitResult();
    } else if (auto *ifex = dynamic_cast<const IfExpr *>(expr->expr.get())) {
      auto result = lowerIf(*ifex, ctx);
      if (!result.ok) {
        return failResult();
      } else if (result.terminated) {
        return result;
      }
      return unitResult();
    } else if (auto *whileex =
                   dynamic_cast<const WhileExpr *>(expr->expr.get())) {
      auto result = lowerWhile(*whileex, ctx);
      if (!result.ok) {
        return failResult();
      } else if (result.terminated) {
        return result;
      }
      return unitResult();
    } else if (auto *loop = dynamic_cast<const LoopExpr *>(expr->expr.get())) {
      auto result = lowerLoop(*loop, ctx);
      if (!result.ok) {
        return failResult();
      } else if (result.terminated) {
        return result;
      }
      return unitResult();
    } else if (auto *brk = dynamic_cast<const BreakExpr *>(expr->expr.get())) {
      return lowerBreak(*brk, ctx);
    } else if (auto *co =
                   dynamic_cast<const ContinueExpr *>(expr->expr.get())) {
      return lowerContinue(*co, ctx);
    } else if (auto *ret = dynamic_cast<const ReturnExpr *>(expr->expr.get())) {
      return lowerReturn(*ret, ctx);
    } else if (auto *call = dynamic_cast<const CallExpr *>(expr->expr.get())) {
      auto result = lowerCall(*call, ctx);
      if (!result.ok) {
        return failResult();
      } else if (result.terminated) {
        return result;
      }
      return unitResult();
    } else if (auto *ass = dynamic_cast<const AssignExpr *>(expr->expr.get())) {
      if (ass == nullptr) {
        return failResult();
      }
      return lowerAssign(*ass, ctx);
    }
    auto result = lowerExpr(*expr->expr, ctx);
    if (!result.ok) {
      return failResult();
    } else if (result.terminated) {
      return result;
    }
    return unitResult();
  }
  return failResult();
}
ir::LowerResult lowerBlock(const BlockExpr &block, LowerContext &ctx) {
  for (const auto &stmt : block.stmts) {
    auto result = lowerStmt(*stmt, ctx);
    if (!result.ok) {
      return failResult();
    } else if (result.terminated) {
      return result;
    }
  }
  if (block.tail == nullptr) {
    return unitResult();
  }
  if (auto *ret = dynamic_cast<const ReturnExpr *>(block.tail.get())) {
    return lowerReturn(*ret, ctx);
  }
  return lowerExpr(*block.tail, ctx);
}
ir::LowerResult lowerIf(const IfExpr &ifex, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  auto cond = lowerExpr(*ifex.cond, ctx);
  if (!cond.ok) {
    return failResult();
  } else if (cond.terminated) {
    return cond;
  } else if (!cond.hasValue) {
    return failResult();
  }
  TypeId resultType = analyzer.exprType(ifex);
  TypeKind kind = analyzer.typeArena().get(resultType).kind;
  bool hasValue = (kind != TypeKind::Unit && kind != TypeKind::Never);
  if (ifex.elseBranch == nullptr && hasValue) {
    return failResult();
  }
  std::string resultSlot = "";
  if (hasValue) {
    resultSlot = builder.ensureTemp(types.valueType(resultType));
  }
  std::string thenLabel = builder.newLabel("then");
  std::string elseLabel = builder.newLabel("else");
  std::string mergeLabel = builder.newLabel("merge");
  builder.condBranch(cond.value, thenLabel, elseLabel);
  builder.startBlock(thenLabel);
  auto thenResult = lowerBlock(*ifex.then, ctx);
  if (!thenResult.ok) {
    return failResult();
  }
  if (!thenResult.terminated) {
    if (hasValue) {
      if (!thenResult.hasValue) {
        return failResult();
      }
      builder.store(types.valueType(resultType), thenResult.value, resultSlot);
    }
    builder.branch(mergeLabel);
  }
  builder.startBlock(elseLabel);
  ir::LowerResult elseResult;
  if (ifex.elseBranch == nullptr) {
    elseResult = unitResult();
  } else if (auto *block =
                 dynamic_cast<const BlockExpr *>(ifex.elseBranch.get())) {
    elseResult = lowerBlock(*block, ctx);
  } else if (auto *innerif =
                 dynamic_cast<const IfExpr *>(ifex.elseBranch.get())) {
    elseResult = lowerExpr(*innerif, ctx);
  } else {
    return failResult();
  }
  if (!elseResult.ok) {
    return failResult();
  }
  if (!elseResult.terminated) {
    if (hasValue) {
      builder.store(types.valueType(resultType), elseResult.value, resultSlot);
    }
    builder.branch(mergeLabel);
  }
  if (thenResult.terminated && elseResult.terminated) {
    return terminatedResult();
  }
  builder.startBlock(mergeLabel);
  if (!hasValue) {
    return unitResult();
  }
  return valueResult(builder.load(types.valueType(resultType), resultSlot));
}
ir::LowerResult lowerBreak(const BreakExpr &brk, LowerContext &ctx) {
  if (ctx.loops.empty()) {
    return failResult();
  }
  LoopContext loop = ctx.loops.back();
  bool hasResult = (loop.resultType != InvalidType &&
                    !isUnitType(loop.resultType, ctx.analyzer));
  if (brk.value != nullptr) {
    if (!hasResult || loop.resultSlot.empty()) {
      return failResult();
    }
    auto value = lowerExpr(*brk.value, ctx);
    if (!value.ok || !value.hasValue) {
      return failResult();
    }
    ctx.builder.store(ctx.valueType(loop.resultType), value.value,
                      loop.resultSlot);
  } else if (hasResult) {
    return failResult();
  }
  ctx.builder.branch(loop.breakLabel);
  return terminatedResult();
}
ir::LowerResult lowerContinue(const ContinueExpr &expr, LowerContext &ctx) {
  if (ctx.loops.empty()) {
    return failResult();
  }
  ctx.builder.branch(ctx.loops.back().continueLabel);
  return terminatedResult();
}
ir::LowerResult lowerWhile(const WhileExpr &whileex, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  std::string condLabel = builder.newLabel("cond");
  std::string bodyLabel = builder.newLabel("body");
  std::string exitLabel = builder.newLabel("exit");
  builder.branch(condLabel);
  builder.startBlock(condLabel);
  auto condResult = lowerExpr(*whileex.cond, ctx);
  if (!condResult.ok) {
    return failResult();
  } else if (condResult.terminated) {
    return condResult;
  } else if (!condResult.hasValue) {
    return failResult();
  }
  builder.condBranch(condResult.value, bodyLabel, exitLabel);
  ctx.loops.push_back(LoopContext{exitLabel, condLabel, "", InvalidType, true});
  builder.startBlock(bodyLabel);
  auto bodyResult = lowerBlock(*whileex.body, ctx);
  if (!bodyResult.ok) {
    ctx.loops.pop_back();
    return failResult();
  }
  if (!bodyResult.terminated) {
    builder.branch(condLabel);
  }
  ctx.loops.pop_back();
  builder.startBlock(exitLabel);
  return unitResult();
}
ir::LowerResult lowerLoop(const LoopExpr &loop, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  auto resultType = analyzer.exprType(loop);
  TypeKind kind = analyzer.typeArena().get(resultType).kind;
  bool hasValue = (kind != TypeKind::Unit && kind != TypeKind::Never);
  std::string retSlot = "";
  if (hasValue) {
    retSlot = builder.ensureTemp(types.valueType(resultType));
  }
  std::string bodyLabel = builder.newLabel("loop.body");
  std::string exitLabel = builder.newLabel("loop.exit");
  builder.branch(bodyLabel);
  builder.startBlock(bodyLabel);
  ctx.loops.push_back(LoopContext{exitLabel, bodyLabel, retSlot, resultType});
  auto bodyResult = lowerBlock(*loop.body, ctx);
  if (!bodyResult.ok) {
    ctx.loops.pop_back();
    return failResult();
  }
  if (!bodyResult.terminated) {
    builder.branch(bodyLabel);
  }
  ctx.loops.pop_back();
  if (kind == TypeKind::Never) {
    return terminatedResult();
  }
  builder.startBlock(exitLabel);
  if (hasValue) {
    return valueResult(builder.load(types.valueType(resultType), retSlot));
  } else {
    return unitResult();
  }
}
ir::LowerResult lowerLazyBool(const BinaryExpr &expr, LowerContext &ctx) {
  auto &builder = ctx.builder;

  BinaryOp op = expr.op;
  auto lhs = lowerExpr(*expr.lhs, ctx);
  if (!lhs.ok) {
    return failResult();
  } else if (lhs.terminated) {
    return lhs;
  } else if (!lhs.hasValue) {
    return failResult();
  }
  TypeId resultType = ctx.exprType(expr);
  std::string resultSlot = builder.ensureTemp(ctx.valueType(resultType));
  std::string rhsLabel =
      builder.newLabel(op == BinaryOp::LAnd ? "and.rhs" : "or.rhs");
  std::string shortLabel =
      builder.newLabel(op == BinaryOp::LAnd ? "and.short" : "or.short");
  std::string mergeLabel =
      builder.newLabel(op == BinaryOp::LAnd ? "and.merge" : "or.merge");
  if (op == BinaryOp::LAnd) {
    builder.condBranch(lhs.value, rhsLabel, shortLabel);
  } else {
    builder.condBranch(lhs.value, shortLabel, rhsLabel);
  }
  builder.startBlock(shortLabel);
  builder.store("i1", (op == BinaryOp::LAnd ? "false" : "true"), resultSlot);
  builder.branch(mergeLabel);
  builder.startBlock(rhsLabel);
  auto rhs = lowerExpr(*expr.rhs, ctx);
  if (!rhs.ok) {
    return failResult();
  }
  if (!rhs.terminated) {
    if (!rhs.hasValue) {
      return failResult();
    }
    builder.store("i1", rhs.value, resultSlot);
    builder.branch(mergeLabel);
  }
  builder.startBlock(mergeLabel);
  return valueResult(builder.load("i1", resultSlot));
}
ir::LowerResult lowerCall(const CallExpr &call, LowerContext &ctx) {
  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr) {
    return failResult();
  }
  int target = ctx.analyzer.bindingOf(*path);
  if (target == InvalidSymbol) {
    return failResult();
  }
  const Symbol *sym = ctx.analyzer.symbol(target);
  if (sym == nullptr) {
    return failResult();
  }
  const std::vector<TypeId> *params = ctx.analyzer.paramTypes(target);
  TypeId returnId = ctx.analyzer.returnType(target);
  if (params == nullptr || params->size() != call.args.size() ||
      returnId == InvalidType) {
    return failResult();
  }
  std::vector<std::string> args;
  for (std::size_t i = 0; i < call.args.size(); ++i) {
    auto arg = lowerExpr(*call.args[i], ctx);
    if (!arg.ok) {
      return failResult();
    } else if (arg.terminated) {
      return arg;
    } else if (!arg.hasValue) {
      return failResult();
    }
    std::string argType = ctx.types.valueType((*params)[i]);
    if (argType.empty()) {
      return failResult();
    }
    args.push_back(argType + " " + arg.value);
  }
  std::string callee = callTargetName(*sym, ctx.analyzer);
  std::string retType = ctx.types.valueType(returnId);
  if (callee.empty() || retType.empty()) {
    return failResult();
  }
  std::string result = ctx.builder.call(retType, callee, args);
  if (retType == "void") {
    return unitResult();
  }
  return valueResult(result);
}
ir::LowerResult lowerMethodCall(const MethodCallExpr &method,
                                LowerContext &ctx) {
  int target = ctx.analyzer.methodSymbol(method);
  if (target == InvalidSymbol) {
    return failResult();
  }
}

bool supportsBinaryOp(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div:
  case BinaryOp::Rem:
  case BinaryOp::BitAnd:
  case BinaryOp::BitOr:
  case BinaryOp::BitXor:
  case BinaryOp::Shl:
  case BinaryOp::Shr:
  case BinaryOp::Eq:
  case BinaryOp::Ne:
  case BinaryOp::Lt:
  case BinaryOp::Le:
  case BinaryOp::Gt:
  case BinaryOp::Ge:
  case BinaryOp::LAnd:
  case BinaryOp::LOr:
    return true;
  default:
    return false;
  }
}
bool supportsExpr(const Expr &expr, const SemanticAnalyzer &analyzer) {
  if (auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    if (unary->op != UnaryOp::Neg && unary->op != UnaryOp::Not) {
      return false;
    }
    return supportsExpr(*unary->operand, analyzer);
  }
  if (auto *cast = dynamic_cast<const CastExpr *>(&expr)) {
    return supportsExpr(*cast->expr, analyzer) &&
           analyzer.exprType(*cast->expr) != InvalidType &&
           analyzer.exprType(*cast) != InvalidType;
  }
  if (dynamic_cast<const UnitLit *>(&expr)) {
    return true;
  }
  if (dynamic_cast<const IntLit *>(&expr)) {
    return true;
  }
  if (auto *path = dynamic_cast<const PathExpr *>(&expr)) {
    return path->segments.size() == 1 &&
           analyzer.bindingOf(*path) != InvalidSymbol;
  }
  if (dynamic_cast<const BoolLit *>(&expr)) {
    return true;
  }
  if (auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    auto *path = dynamic_cast<const PathExpr *>(call->callee.get());
    if (path == nullptr) {
      return false;
    }
    int target = analyzer.bindingOf(*path);
    if (target == InvalidSymbol) {
      return false;
    }
    const Symbol *sym = analyzer.symbol(target);
    if (sym == nullptr) {
      return false;
    }
    if (sym->kind != SymbolKind::Fn && sym->kind != SymbolKind::BuiltinFn &&
        sym->kind != SymbolKind::AssocFn) {
      return false;
    }
    const std::vector<TypeId> *params = analyzer.paramTypes(target);
    if (params == nullptr || params->size() != call->args.size() ||
        analyzer.returnType(target) == InvalidType) {
      return false;
    }
    for (const auto &arg : call->args) {
      if (!supportsExpr(*arg, analyzer)) {
        return false;
      }
    }
    return true;
  }
  if (auto *block = dynamic_cast<const BlockExpr *>(&expr)) {
    return supportsBlock(*block, analyzer);
  }
  if (auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    return supportsBinaryOp(binary->op) &&
           supportsExpr(*binary->lhs, analyzer) &&
           supportsExpr(*binary->rhs, analyzer);
  }
  if (auto *ifex = dynamic_cast<const IfExpr *>(&expr)) {
    if (!supportsExpr(*ifex->cond, analyzer)) {
      return false;
    }
    if (!supportsBlock(*ifex->then, analyzer)) {
      return false;
    }
    if (ifex->elseBranch != nullptr) {
      if (auto *block =
              dynamic_cast<const BlockExpr *>(ifex->elseBranch.get())) {
        return supportsBlock(*block, analyzer);
      }
      if (auto *nested = dynamic_cast<const IfExpr *>(ifex->elseBranch.get())) {
        return supportsExpr(*nested, analyzer);
      }
      return false;
    }
    return true;
  }
  if (auto *whileex = dynamic_cast<const WhileExpr *>(&expr)) {
    return supportsExpr(*whileex->cond, analyzer) &&
           supportsBlock(*whileex->body, analyzer);
  }
  if (auto *loop = dynamic_cast<const LoopExpr *>(&expr)) {
    return supportsBlock(*loop->body, analyzer);
  }
  if (auto *brk = dynamic_cast<const BreakExpr *>(&expr)) {
    return brk->value == nullptr || supportsExpr(*brk->value, analyzer);
  }
  if (dynamic_cast<const ContinueExpr *>(&expr) != nullptr) {
    return true;
  }
  if (auto *ret = dynamic_cast<const ReturnExpr *>(&expr)) {
    return ret->value == nullptr || supportsExpr(*ret->value, analyzer);
  }
  return false;
}
bool supportsStmt(const Stmt &stmt, const SemanticAnalyzer &analyzer) {
  if (auto *let = dynamic_cast<const LetStmt *>(&stmt)) {
    if (let->init == nullptr) {
      return false;
    }
    if (!supportsExpr(*let->init, analyzer)) {
      return false;
    }

    int id = analyzer.symbolIdOfDecl(*let);
    const Symbol *sym = analyzer.symbol(id);
    if (sym == nullptr || !isScalarValueOrNever(sym->type, analyzer)) {
      return false;
    }

    TypeId initType = analyzer.exprType(*let->init);
    return isScalarValueOrNever(initType, analyzer);
  }

  if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    if (dynamic_cast<const BlockExpr *>(expr->expr.get()) != nullptr ||
        dynamic_cast<const IfExpr *>(expr->expr.get()) != nullptr ||
        dynamic_cast<const WhileExpr *>(expr->expr.get()) != nullptr ||
        dynamic_cast<const LoopExpr *>(expr->expr.get()) != nullptr ||
        dynamic_cast<const BreakExpr *>(expr->expr.get()) != nullptr ||
        dynamic_cast<const ContinueExpr *>(expr->expr.get()) != nullptr) {
      return supportsExpr(*expr->expr, analyzer);
    }
    if (auto *ret = dynamic_cast<const ReturnExpr *>(expr->expr.get())) {
      return ret->value == nullptr || supportsExpr(*ret->value, analyzer);
    }
    if (auto *call = dynamic_cast<const CallExpr *>(expr->expr.get())) {
      return supportsExpr(*call, analyzer);
    }

    auto *assign = dynamic_cast<const AssignExpr *>(expr->expr.get());
    if (assign == nullptr) {
      return supportsExpr(*expr->expr, analyzer);
    }

    auto *path = dynamic_cast<const PathExpr *>(assign->lhs.get());
    if (path == nullptr || path->segments.size() != 1) {
      return false;
    }

    int id = analyzer.bindingOf(*path);
    if (id == InvalidSymbol) {
      return false;
    }

    const Symbol *sym = analyzer.symbol(id);
    if (sym == nullptr || sym->kind != SymbolKind::Local ||
        !isScalarValue(sym->type, analyzer)) {
      return false;
    }
    if (isScalarBool(sym->type, analyzer) &&
        assign->op != AssignOp::Assign) {
      return false;
    }

    return supportsExpr(*assign->rhs, analyzer);
  }

  return false;
}
bool supportsBlock(const BlockExpr &block, const SemanticAnalyzer &analyzer) {
  for (const auto &stmt : block.stmts) {
    if (!supportsStmt(*stmt, analyzer)) {
      return false;
    }
  }
  if (block.tail != nullptr) {
    return supportsExpr(*block.tail, analyzer);
  }
  return true;
}
} // namespace
bool IrGenerator::generate(std::ostream &out) {
  ir::Module module;
  ir::TypeMapper types(analyzer);
  module.addDeclaration("get_i32", "i32", {});
  module.addDeclaration("print_i32", "void", {{"i32", "arg0"}});
  module.addDeclaration("println_i32", "void", {{"i32", "arg0"}});
  const FnDecl *mainFn = nullptr;
  for (const auto &item : crate.items) {
    auto *fn = dynamic_cast<const FnDecl *>(item.get());
    if (fn == nullptr) {
      continue;
    } else if (fn->name == "main") {
      mainFn = fn;
      continue;
    }
    int symbol = analyzer.fnSymbol(*fn);
    TypeId res = analyzer.returnType(symbol);
    const std::vector<TypeId> *params = analyzer.paramTypes(symbol);
    if (params == nullptr || !types.declare(res, module)) {
      return false;
    }
    std::vector<ir::FunctionParam> functionParams;
    for (std::size_t i = 0; i < params->size(); ++i) {
      TypeId param = (*params)[i];
      if (!types.declare(param, module)) {
        return false;
      }
      std::string name = (i < fn->params.size()) ? fn->params[i].name
                                                 : "arg" + std::to_string(i);
      functionParams.push_back(ir::FunctionParam{types.valueType(param), name});
    }
    std::unordered_map<int, std::string> paramValues;
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
      int id = analyzer.paramSymbol(fn->params[i]);
      paramValues[id] = "%" + functionParams[i].name;
    }
    std::string irName = "_rx_fn_" + fn->name;
    std::string retType = types.valueType(res);
    if (fn->body != nullptr && supportsBlock(*fn->body, analyzer)) {
      ir::Function &outFn = module.addFunction(irName, retType, functionParams);
      ir::IrBuilder builder(outFn);
      LowerContext ctx{builder, types, analyzer, retType, paramValues};

      auto body = lowerBlock(*fn->body, ctx);
      if (!body.ok) {
        return false;
      }
      if (!body.terminated) {
        if (retType == "void") {
          builder.retVoid();
        } else {
          if (!body.hasValue) {
            return false;
          }
          builder.ret(retType, body.value);
        }
      }
    } else {
      module.addDeclaration(irName, retType, functionParams);
    }
  }
  if (mainFn != nullptr && mainFn->body != nullptr &&
      supportsBlock(*mainFn->body, analyzer)) {
    ir::Function &outFn = module.addFunction(ir::SourceMain, "void", {});
    ir::IrBuilder builder(outFn);
    std::unordered_map<int, std::string> paramValues;
    LowerContext ctx{builder, types, analyzer, "void", paramValues};
    auto body = lowerBlock(*mainFn->body, ctx);
    if (!body.ok) {
      return false;
    }
    if (!body.terminated) {
      if (body.hasValue) {
        return false;
      }
      builder.retVoid();
    }
  } else {
    module.addSourceMain();
  }
  module.addEntryWrapper();
  if (!module.print(out)) {
    std::cerr << "failed to emit a valid IR module\n";
    return false;
  }
  return true;
}
} // namespace rx
