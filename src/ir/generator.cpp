#include "ir/generator.h"

#include <iostream>
#include <optional>
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
std::optional<std::string>
lowerExpr(const Expr &expr, ir::IrBuilder &builder, const ir::TypeMapper &types,
          const SemanticAnalyzer &analyzer,
          const std::unordered_map<int, std::string> &paramValues) {
  if (auto *lit = dynamic_cast<const IntLit *>(&expr)) {
    return std::to_string(lit->value);
  } else if (auto *path = dynamic_cast<const PathExpr *>(&expr)) {
    if (path->segments.size() == 1) {
      int id = analyzer.bindingOf(*path);
      if (id == InvalidSymbol) {
        return std::nullopt;
      }
      auto it = paramValues.find(id);
      if (it != paramValues.end()) {
        return it->second;
      }
      std::string slot = builder.getSlot(id);
      if (slot.empty()) {
        return std::nullopt;
      }
      std::string temp = builder.getTemp();
      TypeId type = analyzer.exprType(expr);
      builder.emit(ir::Instruction{ir::Instruction::Kind::LOAD,
                                   temp,
                                   types.valueType(type),
                                   "",
                                   {slot}});
      return temp;
    }
    return std::nullopt;
  } else if (auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    auto lhs = lowerExpr(*binary->lhs, builder, types, analyzer, paramValues);
    auto rhs = lowerExpr(*binary->rhs, builder, types, analyzer, paramValues);
    if (!lhs || !rhs) {
      return std::nullopt;
    }
    TypeId operandType = analyzer.exprType(*binary->lhs);
    TypeId resultType = analyzer.exprType(expr);
    if (operandType == InvalidType || resultType == InvalidType) {
      return std::nullopt;
    }
    const TypeInfo &operandInfo = analyzer.typeArena().get(operandType);
    bool signedOp = isSignedKind(operandInfo.kind);
    std::string operandIr = types.valueType(operandType);
    std::string resultIr = types.valueType(resultType);
    BinaryOp op = binary->op;
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
      std::string temp = builder.getTemp();
      builder.emit(ir::Instruction{
          ir::Instruction::Kind::ICMP, temp, operandIr, opcode, {*lhs, *rhs}});
      return temp;
    }
    std::string actualRhs = *rhs;
    if (op == BinaryOp::Shl || op == BinaryOp::Shr) {
      actualRhs = builder.getTemp();
      builder.emit(ir::Instruction{ir::Instruction::Kind::BINARY,
                                   actualRhs,
                                   "i32",
                                   "and",
                                   {*rhs, "31"}});
    }
    std::string temp = builder.getTemp();
    builder.emit(ir::Instruction{ir::Instruction::Kind::BINARY,
                                 temp,
                                 resultIr,
                                 opcode,
                                 {*lhs, actualRhs}});
    return temp;
  }
  return std::nullopt;
}
std::optional<std::string> lowerPlace(const Expr &expr, ir::IrBuilder &builder,
                                      const SemanticAnalyzer &analyzer) {
  auto *path = dynamic_cast<const PathExpr *>(&expr);
  if (path == nullptr || path->segments.size() != 1) {
    return std::nullopt;
  }
  int id = analyzer.bindingOf(*path);
  if (id == InvalidSymbol) {
    return std::nullopt;
  }
  std::string slot = builder.getSlot(id);
  if (slot.empty()) {
    return std::nullopt;
  }
  return slot;
}
bool lowerAssign(const AssignExpr &assign, ir::IrBuilder &builder,
                 const ir::TypeMapper &types, const SemanticAnalyzer &analyzer,
                 const std::unordered_map<int, std::string> &paramValues) {
  auto place = lowerPlace(*assign.lhs, builder, analyzer);
  TypeId lhsType = analyzer.exprType(*assign.lhs);
  auto rhs = lowerExpr(*assign.rhs, builder, types, analyzer, paramValues);
  if (!place || !isScalarInt(lhsType, analyzer) || !rhs) {
    return false;
  }
  std::string storage = types.storageType(lhsType);
  if (assign.op == AssignOp::Assign) {
    builder.emit(ir::Instruction{
        ir::Instruction::Kind::STORE, "", storage, "", {*rhs, *place}});
    return true;
  }
  std::string old = builder.getTemp();
  builder.emit(
      ir::Instruction{ir::Instruction::Kind::LOAD, old, storage, "", {*place}});
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
    return false;
  }
  std::string actualRhs = *rhs;
  if (shift) {
    actualRhs = builder.getTemp();
    builder.emit(ir::Instruction{
        ir::Instruction::Kind::BINARY, actualRhs, "i32", "and", {*rhs, "31"}});
  }
  std::string result = builder.getTemp();
  builder.emit(ir::Instruction{ir::Instruction::Kind::BINARY,
                               result,
                               storage,
                               opcode,
                               {old, actualRhs}});
  builder.emit(ir::Instruction{
      ir::Instruction::Kind::STORE, "", storage, "", {result, *place}});
  return true;
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
    return true;
  default:
    return false;
  }
}
bool supportsExpr(const Expr &expr, const SemanticAnalyzer &analyzer) {
  if (dynamic_cast<const IntLit *>(&expr)) {
    return true;
  }
  if (auto *path = dynamic_cast<const PathExpr *>(&expr)) {
    return path->segments.size() == 1 &&
           analyzer.bindingOf(*path) != InvalidSymbol;
  }
  if (auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    return supportsBinaryOp(binary->op) &&
           supportsExpr(*binary->lhs, analyzer) &&
           supportsExpr(*binary->rhs, analyzer);
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
    if (sym == nullptr || !isScalarInt(sym->type, analyzer)) {
      return false;
    }

    TypeId initType = analyzer.exprType(*let->init);
    return isScalarInt(initType, analyzer);
  }

  if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    auto *assign = dynamic_cast<const AssignExpr *>(expr->expr.get());
    if (assign == nullptr) {
      return false;
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
        !isScalarInt(sym->type, analyzer)) {
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
  for (const auto &item : crate.items) {
    auto *fn = dynamic_cast<const FnDecl *>(item.get());
    if (fn == nullptr || fn->name == "main") {
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
      for (const auto &stmt : fn->body->stmts) {
        if (auto *let = dynamic_cast<const LetStmt *>(stmt.get())) {
          int id = analyzer.symbolIdOfDecl(*let);
          if (id == InvalidSymbol) {
            return false;
          }
          const Symbol *sym = analyzer.symbol(id);
          if (sym == nullptr || sym->type == InvalidType) {
            return false;
          }
          std::string storage = types.storageType(sym->type);
          std::string slot = builder.ensureSlot(id, let->name, storage);
          auto value =
              lowerExpr(*let->init, builder, types, analyzer, paramValues);
          if (!value) {
            return false;
          }
          builder.emit(ir::Instruction{
              ir::Instruction::Kind::STORE, "", storage, "", {*value, slot}});
        } else if (auto *expr = dynamic_cast<const ExprStmt *>(stmt.get())) {
          auto *assign = dynamic_cast<const AssignExpr *>(expr->expr.get());
          if (assign == nullptr) {
            return false;
          }
          if (!lowerAssign(*assign, builder, types, analyzer, paramValues)) {
            return false;
          }
        } else {
          return false;
        }
      }
      if (fn->body->tail != nullptr) {
        auto value =
            lowerExpr(*fn->body->tail, builder, types, analyzer, paramValues);
        if (!value) {
          return false;
        }
        builder.emit(ir::Instruction{
            ir::Instruction::Kind::RET, "", retType, "", {*value}});
      } else {
        builder.emit(
            ir::Instruction{ir::Instruction::Kind::RET, "", "void", "", {}});
      }
    } else {
      module.addDeclaration(irName, retType, functionParams);
    }
  }
  module.addSourceMain();
  module.addEntryWrapper();
  if (!module.print(out)) {
    std::cerr << "failed to emit a valid IR module\n";
    return false;
  }
  return true;
}
} // namespace rx
