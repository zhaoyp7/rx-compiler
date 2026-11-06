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
struct LoopContext {
  std::string breakLabel;
  std::string continueLabel;
  std::string resultSlot;
  TypeId resultType = InvalidType;
};
struct LowerContext {
  ir::IrBuilder &builder;
  const ir::TypeMapper &types;
  const SemanticAnalyzer &analyzer;
  const std::string &returnType;
  std::unordered_map<int, std::string> &paramValues;
  std::vector<LoopContext> loops;
  TypeId returnTypeId = InvalidType;

  std::string valueType(TypeId id) const { return types.valueType(id); }
  std::string storageType(TypeId id) const { return types.storageType(id); }
  TypeId exprType(const Expr &expr) const { return analyzer.exprType(expr); }
};
struct CloneFnInfo {
  const StructDecl *decl;
  TypeId type;
  std::string name;
  ir::Function *fn;
};
struct EqFnInfo {
  const StructDecl *decl;
  TypeId type;
  std::string name;
  ir::Function *fn;
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
  TypeKind kind = analyzer.typeArena().get(id).kind;
  if (kind == TypeKind::I32 || kind == TypeKind::U32) {
    return true;
  } else if (kind == TypeKind::Isize || kind == TypeKind::Usize) {
    return true;
  }
  return false;
}
bool isUnitType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Unit);
}
bool isScalarBool(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Bool);
}
bool isScalarValue(TypeId id, const SemanticAnalyzer &analyzer) {
  return (isScalarInt(id, analyzer) || isScalarBool(id, analyzer));
}
bool isStructType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Struct);
}
bool isArrayType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Array);
}
bool isRefType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Ref);
}
bool isBoxNewCall(const CallExpr &call) {
  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr || path->segments.size() != 2) {
    return false;
  }
  const auto &first = path->segments[0];
  const auto &second = path->segments[1];
  return (first.kind == PathSegmentKind::Ident && first.name == "Box" &&
          second.name == "new");
}
bool isBoxType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Box);
}
bool isVecType(TypeId id, const SemanticAnalyzer &analyzer) {
  return (analyzer.typeArena().get(id).kind == TypeKind::Vec);
}
bool isVecNewCall(const CallExpr &call) {
  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr || path->segments.size() != 2) {
    return false;
  }
  const auto &first = path->segments[0];
  const auto &second = path->segments[1];
  return (first.kind == PathSegmentKind::Ident && first.name == "Vec" &&
          second.kind == PathSegmentKind::Ident && second.name == "new");
}
bool isVecBuiltinCall(const CallExpr &call) {
  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr || path->segments.size() != 2) {
    return false;
  }
  const auto &first = path->segments[0];
  const auto &second = path->segments[1];
  if (first.kind != PathSegmentKind::Ident || first.name != "Vec" ||
      second.kind != PathSegmentKind::Ident) {
    return false;
  }
  return (second.name == "len" || second.name == "is_empty" ||
          second.name == "push" || second.name == "remove");
}
bool isBoxCloneCall(const CallExpr &call) {
  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr || path->segments.size() != 2) {
    return false;
  }
  const auto &first = path->segments[0];
  const auto &second = path->segments[1];
  return (first.kind == PathSegmentKind::Ident && first.name == "Box" &&
          second.kind == PathSegmentKind::Ident && second.name == "clone");
}
bool isVecCloneCall(const CallExpr &call) {
  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr || path->segments.size() != 2) {
    return false;
  }
  const auto &first = path->segments[0];
  const auto &second = path->segments[1];
  return (first.kind == PathSegmentKind::Ident && first.name == "Vec" &&
          second.kind == PathSegmentKind::Ident && second.name == "clone");
}
bool isRefOrBoxType(TypeId id, const SemanticAnalyzer &analyzer) {
  if (id == InvalidType) {
    return false;
  }
  TypeKind kind = analyzer.typeArena().get(id).kind;
  return (kind == TypeKind::Ref || kind == TypeKind::Box);
}
bool isLowerableValue(TypeId id, const SemanticAnalyzer &analyzer) {
  if (isScalarValue(id, analyzer) || isStructType(id, analyzer) ||
      isRefType(id, analyzer)) {
    return true;
  }
  if (isArrayType(id, analyzer) || isBoxType(id, analyzer) ||
      isVecType(id, analyzer)) {
    const TypeInfo &info = analyzer.typeArena().get(id);
    return isLowerableValue(info.inner, analyzer);
  }
  return false;
}
bool isLowerableValueOrNever(TypeId id, const SemanticAnalyzer &analyzer) {
  if (id == InvalidType) {
    return false;
  }
  if (analyzer.typeArena().get(id).kind == TypeKind::Never) {
    return true;
  }
  return isLowerableValue(id, analyzer);
}
std::string callTargetName(const Symbol &sym,
                           const SemanticAnalyzer &analyzer) {
  if (sym.kind == SymbolKind::BuiltinFn) {
    return sym.name;
  } else if (sym.kind == SymbolKind::Fn) {
    return (sym.name == "main" ? "_rx_main" : "_rx_fn_" + sym.name);
  } else if (sym.kind == SymbolKind::AssocFn && sym.owner != InvalidSymbol) {
    const Symbol *owner = analyzer.symbol(sym.owner);
    if (owner == nullptr) {
      return "";
    }
    return "_rx_m_" + owner->name + "_" + sym.name;
  }
  return "";
}
std::string toStorageValue(const LowerContext &ctx, TypeId type,
                           const std::string &value) {
  std::string storage = ctx.types.storageType(type);
  std::string valueType = ctx.types.valueType(type);
  if (storage == "i8" && valueType == "i1") {
    return ctx.builder.cast("zext", "i1", "i8", value);
  }
  return value;
}
std::string fromStorageValue(const LowerContext &ctx, TypeId type,
                             const std::string &value) {
  std::string storage = ctx.types.storageType(type);
  std::string valueType = ctx.types.valueType(type);
  if (storage == "i8" && valueType == "i1") {
    return ctx.builder.cast("trunc", "i8", "i1", value);
  }
  return value;
}
std::string derefValueToType(const std::string &value, TypeId type,
                             LowerContext &ctx) {
  std::string result = value;
  TypeId current = type;
  while (current != InvalidType) {
    const TypeInfo &info = ctx.analyzer.typeArena().get(current);
    if (info.kind != TypeKind::Ref && info.kind != TypeKind::Box) {
      break;
    }
    TypeId inner = info.inner;
    std::string storage = ctx.types.storageType(inner);
    result = ctx.builder.load(storage, result);
    result = fromStorageValue(ctx, inner, result);
    current = inner;
  }
  return result;
}
std::string vecDataPtr(const std::string &vecPtr, LowerContext &ctx) {
  return ctx.builder.gep("%struct.Vec", vecPtr, {"i32 0", "i32 0"});
}
std::string vecLenPtr(const std::string &vecPtr, LowerContext &ctx) {
  return ctx.builder.gep("%struct.Vec", vecPtr, {"i32 0", "i32 1"});
}
std::string vecCapPtr(const std::string &vecPtr, LowerContext &ctx) {
  return ctx.builder.gep("%struct.Vec", vecPtr, {"i32 0", "i32 2"});
}
TypeId strip(TypeId type, const SemanticAnalyzer &analyzer) {
  while (type != InvalidType) {
    const TypeInfo &info = analyzer.typeArena().get(type);
    if (info.kind != TypeKind::Ref && info.kind != TypeKind::Box) {
      break;
    }
    type = info.inner;
  }
  return type;
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
ir::LowerResult lowerBlock(const BlockExpr &block, LowerContext &ctx,
                           TypeId expectedType = InvalidType);
ir::LowerResult lowerReturn(const ReturnExpr &ret, LowerContext &ctx);
ir::LowerResult lowerLazyBool(const BinaryExpr &expr, LowerContext &ctx);
ir::LowerResult lowerCall(const CallExpr &call, LowerContext &ctx);
ir::LowerResult lowerStructLit(const StructLit &lit, LowerContext &ctx);
ir::LowerResult lowerPlace(const Expr &expr, LowerContext &ctx);
ir::LowerResult lowerAssign(const AssignExpr &assign, LowerContext &ctx);
ir::LowerResult lowerArrayLit(const ArrayLit &lit, LowerContext &ctx);
ir::LowerResult lowerArrayRepeat(const ArrayRepeat &arr, LowerContext &ctx);
ir::LowerResult lowerPlaceOrTemp(const Expr &expr, LowerContext &ctx);
ir::LowerResult lowerAddress(const Expr &expr, LowerContext &ctx);
ir::LowerResult lowerMethodCall(const MethodCallExpr &method,
                                LowerContext &ctx);
ir::LowerResult lowerBoxNew(const CallExpr &call, LowerContext &ctx);
ir::LowerResult lowerVecNew(const CallExpr &call, LowerContext &ctx);
ir::LowerResult lowerVecBuiltinCall(const CallExpr &call, LowerContext &ctx);
ir::LowerResult lowerVecPush(const std::string &vecPtr, TypeId elemType,
                             const std::string &value, LowerContext &ctx);
ir::LowerResult lowerVecRemove(const std::string &vecPtr, TypeId elemType,
                               const std::string &index, LowerContext &ctx);
ir::LowerResult lowerVecClone(TypeId vecType, const std::string &vecAddr,
                              LowerContext &ctx);
ir::LowerResult lowerCloneValue(TypeId type, const std::string &value,
                                LowerContext &ctx);
ir::LowerResult lowerVecIsEmpty(const std::string &vecPtr, LowerContext &ctx);
ir::LowerResult lowerCloneStructFields(TypeId structType,
                                       const std::string &selfPtr,
                                       LowerContext &ctx);
ir::LowerResult lowerEquality(TypeId type, const std::string &lhs,
                              const std::string &rhs, LowerContext &ctx);
ir::LowerResult lowerEqualityStructFields(TypeId structType,
                                          const std::string &lhsPtr,
                                          const std::string &rhsPtr,
                                          LowerContext &ctx);
ir::LowerResult coerceValueToType(ir::LowerResult value, TypeId actualType,
                                  TypeId expectedType, LowerContext &ctx);
ir::LowerResult lowerExprToType(const Expr &expr, TypeId expectedType,
                                LowerContext &ctx);
ir::LowerResult lowerReceiverValueToType(const Expr &expr, TypeId selfType,
                                         LowerContext &ctx);

ir::LowerResult lowerExpr(const Expr &expr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  const std::unordered_map<int, std::string> &paramValues = ctx.paramValues;

  if (dynamic_cast<const UnitLit *>(&expr)) {
    return unitResult();
  } else if (auto *lit = dynamic_cast<const IntLit *>(&expr)) {
    return valueResult(std::to_string(lit->value));
  } else if (auto *path = dynamic_cast<const PathExpr *>(&expr)) {
    int id = analyzer.bindingOf(*path);
    if (id != InvalidSymbol) {
      const Symbol *sym = analyzer.symbol(id);
      if (sym != nullptr && (sym->kind == SymbolKind::Const ||
                             sym->kind == SymbolKind::AssocConst)) {
        const ConstVal *value = analyzer.constValue(id);
        if (value == nullptr) {
          return failResult();
        } else if (value->type == ConstType::Bool) {
          return valueResult(value->boolean ? "true" : "false");
        } else {
          return valueResult(std::to_string(value->value));
        }
      }
    }
    if (path->segments.size() == 1) {
      int id = analyzer.bindingOf(*path);
      if (id == InvalidSymbol) {
        return failResult();
      }
      std::string slot = builder.getSlot(id);
      if (!slot.empty()) {
        TypeId type = analyzer.exprType(expr);
        std::string storage = types.storageType(type);
        std::string value = builder.load(storage, slot);
        return valueResult(fromStorageValue(ctx, type, value));
      }
      auto it = paramValues.find(id);
      if (it != paramValues.end()) {
        return valueResult(it->second);
      }
      return failResult();
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
  } else if (auto *assign = dynamic_cast<const AssignExpr *>(&expr)) {
    return lowerAssign(*assign, ctx);
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
    TypeId resultType = analyzer.exprType(expr);
    if (resultType == InvalidType) {
      return failResult();
    }
    TypeId lhsType = analyzer.exprType(*binary->lhs);
    TypeId rhsType = analyzer.exprType(*binary->rhs);
    bool lhsRef = isRefType(lhsType, analyzer);
    bool rhsRef = isRefType(rhsType, analyzer);
    if ((op == BinaryOp::Eq || op == BinaryOp::Ne) && lhsRef && rhsRef) {
      TypeId lhsTarget = strip(lhsType, analyzer);
      TypeId rhsTarget = strip(rhsType, analyzer);
      lhs.value = derefValueToType(lhs.value, lhsType, ctx);
      rhs.value = derefValueToType(rhs.value, rhsType, ctx);
      if (isScalarValue(lhsTarget, analyzer) &&
          isScalarValue(rhsTarget, analyzer)) {
        lhsType = lhsTarget;
        std::string pred = (op == BinaryOp::Eq) ? "eq" : "ne";
        return valueResult(
            builder.icmp(pred, types.valueType(lhsType), lhs.value, rhs.value));
      }
      auto eq = lowerEquality(lhsTarget, lhs.value, rhs.value, ctx);
      if (!eq.ok || eq.terminated || !eq.hasValue) {
        return failResult();
      } else if (op == BinaryOp::Ne) {
        return valueResult(builder.binary("xor", "i1", eq.value, "true"));
      }
      return valueResult(eq.value);
    }
    if (op != BinaryOp::Eq && op != BinaryOp::Ne) {
      if (lhsRef) {
        lhs.value = derefValueToType(lhs.value, lhsType, ctx);
        lhsType = strip(lhsType, analyzer);
      }
      if (rhsRef) {
        rhs.value = derefValueToType(rhs.value, rhsType, ctx);
        rhsType = strip(rhsType, analyzer);
      }
    }
    if ((op == BinaryOp::Eq || op == BinaryOp::Ne) &&
        !isScalarValue(lhsType, analyzer)) {
      auto eq = lowerEquality(lhsType, lhs.value, rhs.value, ctx);
      if (!eq.ok || eq.terminated || !eq.hasValue) {
        return failResult();
      } else if (op == BinaryOp::Ne) {
        return valueResult(builder.binary("xor", "i1", eq.value, "true"));
      }
      return valueResult(eq.value);
    }
    const TypeInfo &operandInfo = analyzer.typeArena().get(lhsType);
    bool signedOp = isSignedKind(operandInfo.kind);
    std::string operandIr = types.valueType(lhsType);
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
    UnaryOp op = unary->op;
    if (op == UnaryOp::Ref || op == UnaryOp::RefMut) {
      auto place = lowerPlaceOrTemp(*unary->operand, ctx);
      if (!place.ok || !place.hasValue) {
        return failResult();
      }
      return valueResult(place.value);
    } else if (op == UnaryOp::Deref) {
      auto place = lowerPlace(*unary, ctx);
      if (!place.ok || !place.hasValue) {
        return failResult();
      }
      TypeId type = analyzer.exprType(expr);
      std::string storage = types.storageType(type);
      std::string value = builder.load(storage, place.value);
      return valueResult(fromStorageValue(ctx, type, value));
    } else if (op == UnaryOp::Neg || op == UnaryOp::Not) {
      auto value = lowerExpr(*unary->operand, ctx);
      if (!value.ok) {
        return failResult();
      } else if (value.terminated) {
        return value;
      } else if (!value.hasValue) {
        return failResult();
      }
      TypeId operandType = analyzer.exprType(*unary->operand);
      if (isRefType(operandType, analyzer)) {
        value.value = derefValueToType(value.value, operandType, ctx);
        operandType = strip(operandType, analyzer);
      }
      TypeId type = analyzer.exprType(expr);
      std::string irType = types.valueType(type);
      if (irType.empty()) {
        return failResult();
      }
      if (op == UnaryOp::Neg) {
        if (irType != "i32") {
          return failResult();
        }
        return valueResult(builder.binary("sub", "i32", "0", value.value));
      } else if (op == UnaryOp::Not) {
        if (irType == "i1") {
          return valueResult(builder.binary("xor", "i1", value.value, "true"));
        } else if (irType == "i32") {
          return valueResult(builder.binary("xor", "i32", value.value, "-1"));
        }
      }
    }
    return failResult();
  } else if (auto *stru = dynamic_cast<const StructLit *>(&expr)) {
    return lowerStructLit(*stru, ctx);
  } else if (auto *field = dynamic_cast<const FieldAccessExpr *>(&expr)) {
    auto place = lowerPlace(*field, ctx);
    if (!place.ok || !place.hasValue) {
      return failResult();
    }
    TypeId fieldType = ctx.analyzer.exprType(expr);
    if (fieldType == InvalidType) {
      return failResult();
    }
    std::string loaded =
        ctx.builder.load(ctx.types.storageType(fieldType), place.value);
    return valueResult(fromStorageValue(ctx, fieldType, loaded));
  } else if (auto *index = dynamic_cast<const IndexExpr *>(&expr)) {
    auto place = lowerPlace(*index, ctx);
    if (!place.ok || !place.hasValue) {
      return failResult();
    }
    TypeId elemType = ctx.analyzer.exprType(expr);
    std::string loaded =
        ctx.builder.load(ctx.types.storageType(elemType), place.value);
    return valueResult(fromStorageValue(ctx, elemType, loaded));
  } else if (auto *array = dynamic_cast<const ArrayLit *>(&expr)) {
    return lowerArrayLit(*array, ctx);
  } else if (auto *repeat = dynamic_cast<const ArrayRepeat *>(&expr)) {
    return lowerArrayRepeat(*repeat, ctx);
  } else if (auto *method = dynamic_cast<const MethodCallExpr *>(&expr)) {
    return lowerMethodCall(*method, ctx);
  }
  return failResult();
}
ir::LowerResult lowerPlace(const Expr &expr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (auto *path = dynamic_cast<const PathExpr *>(&expr)) {
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
  } else if (auto *field = dynamic_cast<const FieldAccessExpr *>(&expr)) {
    auto base = lowerAddress(*field->receiver, ctx);
    if (!base.ok || !base.hasValue) {
      return failResult();
    }
    TypeId receiverType = strip(analyzer.exprType(*field->receiver), analyzer);
    if (receiverType == InvalidType) {
      return failResult();
    }
    const TypeInfo &info = analyzer.typeArena().get(receiverType);
    if (info.kind != TypeKind::Struct) {
      return failResult();
    }
    int idx = analyzer.structFieldIndex(info.structId, field->field);
    if (idx < 0) {
      return failResult();
    }
    std::string structType = types.valueType(receiverType);
    std::string fieldPtr = builder.gep(structType, base.value,
                                       {"i32 0", "i32 " + std::to_string(idx)});
    return valueResult(fieldPtr);
  } else if (auto *index = dynamic_cast<const IndexExpr *>(&expr)) {
    auto base = lowerAddress(*index->base, ctx);
    if (!base.ok || !base.hasValue) {
      return failResult();
    }
    auto indexValue = lowerExpr(*index->index, ctx);
    if (!indexValue.ok || indexValue.terminated || !indexValue.hasValue) {
      return failResult();
    }
    TypeId baseType = strip(analyzer.exprType(*index->base), analyzer);
    if (baseType == InvalidType) {
      return failResult();
    }
    const TypeInfo &info = analyzer.typeArena().get(baseType);
    if (info.kind == TypeKind::Array) {
      std::string arrayType = types.valueType(baseType);
      std::string elemPtr = builder.gep(arrayType, base.value,
                                        {"i32 0", "i32 " + indexValue.value});
      return valueResult(elemPtr);
    } else if (info.kind == TypeKind::Vec) {
      std::string dataAddr = vecDataPtr(base.value, ctx);
      std::string data = builder.load("ptr", dataAddr);
      std::string elemPtr = builder.gep(types.storageType(info.inner), data,
                                        {"i32 " + indexValue.value});
      return valueResult(elemPtr);
    }
  } else if (auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    if (unary->op == UnaryOp::Deref) {
      auto value = lowerExpr(*unary->operand, ctx);
      if (!value.ok || value.terminated || !value.hasValue) {
        return failResult();
      }
      return valueResult(value.value);
    }
  }
  return failResult();
}
ir::LowerResult lowerAssign(const AssignExpr &assign, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  TypeId lhsType = analyzer.exprType(*assign.lhs);
  if (!isLowerableValueOrNever(lhsType, analyzer)) {
    return failResult();
  }
  std::string storage = types.storageType(lhsType);
  if (assign.op == AssignOp::Assign) {
    auto rhs = lowerExprToType(*assign.rhs, lhsType, ctx);
    if (!rhs.ok) {
      return failResult();
    } else if (rhs.terminated) {
      return rhs;
    } else if (!rhs.hasValue) {
      return failResult();
    }
    auto place = lowerPlace(*assign.lhs, ctx);
    if (!place.ok || !place.hasValue) {
      return failResult();
    }
    builder.store(storage, toStorageValue(ctx, lhsType, rhs.value),
                  place.value);
    return unitResult();
  }
  auto rhs = lowerExpr(*assign.rhs, ctx);
  if (!rhs.ok) {
    return failResult();
  } else if (rhs.terminated) {
    return rhs;
  } else if (!rhs.hasValue) {
    return failResult();
  }
  auto place = lowerPlace(*assign.lhs, ctx);
  if (!place.ok || !place.hasValue) {
    return failResult();
  }
  if (isScalarBool(lhsType, analyzer)) {
    std::string old8 = builder.load("i8", place.value);
    std::string old = fromStorageValue(ctx, lhsType, old8);
    std::string opcode;
    if (assign.op == AssignOp::BitAndAssign) {
      opcode = "and";
    } else if (assign.op == AssignOp::BitOrAssign) {
      opcode = "or";
    } else if (assign.op == AssignOp::BitXorAssign) {
      opcode = "xor";
    } else {
      return failResult();
    }
    std::string rhsValue = rhs.value;
    TypeId rhsType = analyzer.exprType(*assign.rhs);
    if (isRefType(rhsType, analyzer)) {
      rhsValue = derefValueToType(rhsValue, rhsType, ctx);
    }
    std::string result = builder.binary(opcode, "i1", old, rhsValue);
    std::string stored = toStorageValue(ctx, lhsType, result);
    builder.store("i8", stored, place.value);
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
  TypeId rhsType = analyzer.exprType(*assign.rhs);
  std::string rhsValue = rhs.value;
  if (isRefType(rhsType, analyzer)) {
    rhsValue = derefValueToType(rhsValue, rhsType, ctx);
  }
  std::string actualRhs = rhsValue;
  if (shift) {
    actualRhs = builder.binary("and", "i32", rhsValue, "31");
  }
  std::string result = builder.binary(opcode, storage, old, actualRhs);
  builder.store(storage, result, place.value);
  return unitResult();
}
ir::LowerResult lowerReturn(const ReturnExpr &ret, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (ret.value == nullptr) {
    builder.retVoid();
    return terminatedResult();
  }
  auto ans = lowerExprToType(*ret.value, ctx.returnTypeId, ctx);
  if (!ans.ok) {
    return failResult();
  } else if (ans.terminated) {
    return ans;
  } else if (ans.hasValue) {
    if (ctx.returnType == "void") {
      return failResult();
    }
    builder.ret(ctx.returnType, ans.value);
  } else {
    if (ctx.returnType != "void") {
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
    auto value = lowerExprToType(*let->init, sym->type, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    std::string storage = types.storageType(sym->type);
    std::string slot = builder.ensureSlot(id, let->name, storage);
    builder.store(storage, toStorageValue(ctx, sym->type, value.value), slot);
    return unitResult();
  } else if (auto *expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    auto result = lowerExpr(*expr->expr, ctx);
    if (!result.ok || result.terminated) {
      return result;
    }
    return unitResult();
  }
  return failResult();
}
ir::LowerResult lowerBlock(const BlockExpr &block, LowerContext &ctx,
                           TypeId expectedType) {
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
  return lowerExprToType(*block.tail, expectedType, ctx);
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
  auto thenResult = lowerBlock(*ifex.then, ctx, resultType);
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
  } else {
    elseResult = lowerExprToType(*ifex.elseBranch, resultType, ctx);
  }
  if (!elseResult.ok) {
    return failResult();
  } else if (!elseResult.terminated) {
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
    auto value = lowerExprToType(*brk.value, loop.resultType, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    }
    if (hasResult) {
      if (!value.hasValue || loop.resultSlot.empty()) {
        return failResult();
      }
      ctx.builder.store(ctx.valueType(loop.resultType), value.value,
                        loop.resultSlot);
    } else if (value.hasValue) {
      return failResult();
    }
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
  ctx.loops.push_back(LoopContext{exitLabel, condLabel, "", InvalidType});
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
  } else if (!bodyResult.terminated) {
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
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

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
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (isBoxNewCall(call)) {
    return lowerBoxNew(call, ctx);
  } else if (isVecNewCall(call)) {
    return lowerVecNew(call, ctx);
  } else if (isBoxCloneCall(call) || isVecCloneCall(call)) {
    if (call.args.size() != 1) {
      return failResult();
    }
    TypeId selfType = analyzer.exprType(call);
    if (!isBoxType(selfType, analyzer) && !isVecType(selfType, analyzer)) {
      return failResult();
    }
    auto receiver = lowerReceiverValueToType(*call.args[0], selfType, ctx);
    if (!receiver.ok || receiver.terminated) {
      return receiver;
    }
    if (!receiver.hasValue) {
      return failResult();
    }
    return lowerCloneValue(selfType, receiver.value, ctx);
  } else if (isVecBuiltinCall(call)) {
    return lowerVecBuiltinCall(call, ctx);
  }
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
  if (sym->kind == SymbolKind::AssocFn && sym->decl == nullptr &&
      sym->name == "clone") {
    if (call.args.size() != 1) {
      return failResult();
    }
    auto self = lowerAddress(*call.args[0], ctx);
    if (!self.ok || self.terminated || !self.hasValue) {
      return failResult();
    }
    const Symbol *owner = analyzer.symbol(sym->owner);
    if (owner == nullptr) {
      return failResult();
    }
    TypeId result = analyzer.exprType(call);
    std::string callResult =
        builder.call(types.valueType(result), "_rx_clone_" + owner->name,
                     {"ptr " + self.value});
    return valueResult(callResult);
  }
  const std::vector<TypeId> *params = ctx.analyzer.paramTypes(target);
  TypeId returnId = ctx.analyzer.returnType(target);
  if (params == nullptr || params->size() != call.args.size() ||
      returnId == InvalidType) {
    return failResult();
  }
  std::vector<std::string> args;
  for (std::size_t i = 0; i < call.args.size(); ++i) {
    TypeId paramType = (*params)[i];
    auto arg = lowerExprToType(*call.args[i], paramType, ctx);
    if (!arg.ok) {
      return failResult();
    } else if (arg.terminated) {
      return arg;
    } else if (!arg.hasValue) {
      return failResult();
    }

    std::string argType = ctx.types.valueType(paramType);
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
ir::LowerResult lowerStructLit(const StructLit &lit, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  TypeId type = analyzer.exprType(lit);
  if (type == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = analyzer.typeArena().get(type);
  if (info.kind != TypeKind::Struct) {
    return failResult();
  }
  std::string irType = types.valueType(type);
  std::string temp = builder.ensureTemp(types.storageType(type));
  for (const auto &init : lit.fields) {
    int idx = analyzer.structFieldIndex(info.structId, init.name);
    if (idx < 0) {
      return failResult();
    }
    TypeId fieldType = (*analyzer.structFields(info.structId))[idx];
    std::string fieldPtr =
        builder.gep(irType, temp, {"i32 0", "i32 " + std::to_string(idx)});
    auto value = lowerExprToType(*init.value, fieldType, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    builder.store(types.storageType(fieldType),
                  toStorageValue(ctx, fieldType, value.value), fieldPtr);
  }
  return valueResult(ctx.builder.load(ctx.types.storageType(type), temp));
}
ir::LowerResult lowerArrayLit(const ArrayLit &lit, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  TypeId type = analyzer.exprType(lit);
  if (type == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = analyzer.typeArena().get(type);
  if (info.kind != TypeKind::Array) {
    return failResult();
  }
  TypeId inner = info.inner;
  int len = info.len;
  std::string arrayType = types.valueType(type);
  std::string temp = ctx.builder.ensureTemp(types.storageType(type));
  for (std::size_t i = 0; i < lit.elements.size(); ++i) {
    auto value = lowerExprToType(*lit.elements[i], info.inner, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    std::string elemPtr =
        builder.gep(arrayType, temp, {"i32 0", "i32 " + std::to_string(i)});
    builder.store(types.storageType(info.inner),
                  toStorageValue(ctx, info.inner, value.value), elemPtr);
  }
  return valueResult(builder.load(types.storageType(type), temp));
}
ir::LowerResult lowerArrayRepeat(const ArrayRepeat &arr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  TypeId type = analyzer.exprType(arr);
  if (type == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = analyzer.typeArena().get(type);
  if (info.kind != TypeKind::Array) {
    return failResult();
  }
  TypeId inner = info.inner;
  auto value = lowerExprToType(*arr.elem, inner, ctx);
  if (!value.ok) {
    return failResult();
  } else if (value.terminated) {
    return value;
  } else if (!value.hasValue) {
    return failResult();
  }
  std::string arrayType = types.valueType(type);
  std::string temp = builder.ensureTemp(types.storageType(type));
  for (int i = 0; i < info.len; ++i) {
    std::string ptr =
        builder.gep(arrayType, temp, {"i32 0", "i32 " + std::to_string(i)});
    builder.store(types.storageType(info.inner),
                  toStorageValue(ctx, info.inner, value.value), ptr);
  }
  return valueResult(builder.load(types.storageType(type), temp));
}
ir::LowerResult lowerMethodCall(const MethodCallExpr &method,
                                LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  int target = analyzer.methodSymbol(method);
  TypeId baseType = strip(analyzer.exprType(*method.receiver), analyzer);
  if (target != InvalidSymbol) {
    const Symbol *sym = analyzer.symbol(target);
    if (sym != nullptr && sym->kind == SymbolKind::AssocFn &&
        sym->decl == nullptr && sym->name == "clone") {
      auto self = lowerAddress(*method.receiver, ctx);
      if (!self.ok || self.terminated || !self.hasValue) {
        return failResult();
      }
      const Symbol *owner = analyzer.symbol(sym->owner);
      if (owner == nullptr) {
        return failResult();
      }
      TypeId result = analyzer.exprType(method);
      std::string call =
          builder.call(types.valueType(result), "_rx_clone_" + owner->name,
                       {"ptr " + self.value});
      return valueResult(call);
    }
  }
  if (target == InvalidSymbol && isVecType(baseType, analyzer)) {
    const TypeInfo &info = analyzer.typeArena().get(baseType);
    TypeId elemType = info.inner;
    if (method.method == "is_empty" && method.args.empty()) {
      auto vec = lowerAddress(*method.receiver, ctx);
      if (!vec.ok || vec.terminated || !vec.hasValue) {
        return failResult();
      }
      return lowerVecIsEmpty(vec.value, ctx);
    }
    if (method.method == "push" && method.args.size() == 1) {
      auto vec = lowerAddress(*method.receiver, ctx);
      if (!vec.ok || vec.terminated || !vec.hasValue) {
        return failResult();
      }
      auto value = lowerExprToType(*method.args[0], elemType, ctx);
      if (!value.ok) {
        return failResult();
      } else if (value.terminated) {
        return value;
      } else if (!value.hasValue) {
        return failResult();
      }
      return lowerVecPush(vec.value, elemType, value.value, ctx);
    }
    if (method.method == "remove" && method.args.size() == 1) {
      auto vec = lowerAddress(*method.receiver, ctx);
      if (!vec.ok || vec.terminated || !vec.hasValue) {
        return failResult();
      }
      auto index = lowerExpr(*method.args[0], ctx);
      if (!index.ok) {
        return failResult();
      } else if (index.terminated) {
        return index;
      } else if (!index.hasValue) {
        return failResult();
      }
      return lowerVecRemove(vec.value, elemType, index.value, ctx);
    }
  }
  if (method.method == "len" && method.args.empty()) {
    TypeId vecType = strip(analyzer.exprType(*method.receiver), analyzer);
    if (isVecType(vecType, analyzer)) {
      auto vec = lowerAddress(*method.receiver, ctx);
      if (!vec.ok || vec.terminated || !vec.hasValue) {
        return failResult();
      }
      std::string len = builder.load("i32", vecLenPtr(vec.value, ctx));
      return valueResult(len);
    }
  }
  if (method.method == "len" && method.args.empty()) {
    TypeId receiverType = strip(analyzer.exprType(*method.receiver), analyzer);
    if (receiverType != InvalidType &&
        analyzer.typeArena().get(receiverType).kind == TypeKind::Array) {
      auto receiver = lowerExpr(*method.receiver, ctx);
      if (!receiver.ok) {
        return failResult();
      } else if (receiver.terminated) {
        return receiver;
      }
      int len = analyzer.typeArena().get(receiverType).len;
      return valueResult(std::to_string(len));
    }
  }
  TypeId resultType = analyzer.exprType(method);
  if (target == InvalidSymbol && method.method == "clone" &&
      method.args.empty()) {
    auto receiver = lowerReceiverValueToType(*method.receiver, resultType, ctx);
    if (!receiver.ok || receiver.terminated) {
      return receiver;
    }
    if (!receiver.hasValue) {
      return failResult();
    }

    return lowerCloneValue(resultType, receiver.value, ctx);
  }
  const Symbol *sym = analyzer.symbol(target);
  const std::vector<TypeId> *params = analyzer.paramTypes(target);
  TypeId returnId = analyzer.returnType(target);
  if (sym == nullptr || params == nullptr || params->empty() ||
      params->size() != method.args.size() + 1 || returnId == InvalidType) {
    return failResult();
  }
  std::string callee = callTargetName(*sym, ctx.analyzer);
  if (callee.empty()) {
    return failResult();
  }
  std::vector<std::string> args;
  TypeId receiverType = (*params)[0];
  if (isRefType(receiverType, ctx.analyzer)) {
    auto receiver = lowerAddress(*method.receiver, ctx);
    if (!receiver.ok) {
      return failResult();
    } else if (receiver.terminated) {
      return receiver;
    } else if (!receiver.hasValue) {
      return failResult();
    }
    args.push_back("ptr " + receiver.value);
  } else {
    auto receiver = lowerExpr(*method.receiver, ctx);
    if (!receiver.ok) {
      return failResult();
    } else if (receiver.terminated) {
      return receiver;
    } else if (!receiver.hasValue) {
      return failResult();
    }
    TypeId receiverExprType = analyzer.exprType(*method.receiver);
    if (isRefOrBoxType(receiverExprType, ctx.analyzer)) {
      receiver.value = derefValueToType(receiver.value, receiverExprType, ctx);
    }
    args.push_back(types.valueType(receiverType) + " " + receiver.value);
  }
  for (std::size_t i = 0; i < method.args.size(); ++i) {
    TypeId paramType = (*params)[i + 1];
    auto arg = lowerExprToType(*method.args[i], paramType, ctx);
    if (!arg.ok) {
      return failResult();
    } else if (arg.terminated) {
      return arg;
    } else if (!arg.hasValue) {
      return failResult();
    }
    args.push_back(types.valueType(paramType) + " " + arg.value);
  }
  std::string retType = types.valueType(returnId);
  std::string result = builder.call(retType, callee, args);
  if (retType == "void") {
    return unitResult();
  }
  return valueResult(result);
}
ir::LowerResult lowerPlaceOrTemp(const Expr &expr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  auto place = lowerPlace(expr, ctx);
  if (place.ok) {
    return place;
  }
  auto value = lowerExpr(expr, ctx);
  if (!value.ok) {
    return failResult();
  } else if (value.terminated) {
    return value;
  } else if (!value.hasValue) {
    return failResult();
  }
  TypeId type = analyzer.exprType(expr);
  std::string storage = types.storageType(type);
  std::string temp = builder.ensureTemp(storage);
  builder.store(storage, toStorageValue(ctx, type, value.value), temp);
  return valueResult(temp);
}
ir::LowerResult lowerAddress(const Expr &expr, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  TypeId type = analyzer.exprType(expr);
  if (type == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = analyzer.typeArena().get(type);
  if (info.kind == TypeKind::Ref || info.kind == TypeKind::Box) {
    auto value = lowerExpr(expr, ctx);
    if (!value.ok || value.terminated || !value.hasValue) {
      return failResult();
    }
    std::string address = value.value;
    TypeId inner = info.inner;
    while (inner != InvalidType) {
      const TypeInfo &infoo = ctx.analyzer.typeArena().get(inner);
      if (infoo.kind != TypeKind::Ref && infoo.kind != TypeKind::Box) {
        break;
      }
      address = builder.load("ptr", address);
      inner = infoo.inner;
    }
    return valueResult(address);
  }
  auto place = lowerPlace(expr, ctx);
  if (place.ok) {
    return place;
  }
  return lowerPlaceOrTemp(expr, ctx);
}
ir::LowerResult lowerBoxNew(const CallExpr &call, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (call.args.size() != 1) {
    return failResult();
  }
  TypeId boxType = ctx.analyzer.exprType(call);
  if (boxType == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = ctx.analyzer.typeArena().get(boxType);
  if (info.kind != TypeKind::Box) {
    return failResult();
  }
  TypeId inner = info.inner;
  if (inner == InvalidType) {
    return failResult();
  }
  auto value = lowerExprToType(*call.args[0], inner, ctx);
  if (!value.ok) {
    return failResult();
  } else if (value.terminated) {
    return value;
  } else if (!value.hasValue) {
    return failResult();
  }
  uint32_t size = analyzer.typeSize(inner);
  uint32_t align = analyzer.typeAlign(inner);
  std::string raw = builder.call(
      "ptr", "__rx_alloc",
      {"i32 " + std::to_string(size), "i32 " + std::to_string(align)});
  std::string storage = types.storageType(inner);
  std::string stored = toStorageValue(ctx, inner, value.value);
  builder.store(storage, stored, raw);
  return valueResult(raw);
}
ir::LowerResult lowerVecNew(const CallExpr &call, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (!call.args.empty()) {
    return failResult();
  }
  TypeId vecType = analyzer.exprType(call);
  if (vecType == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = analyzer.typeArena().get(vecType);
  if (info.kind != TypeKind::Vec || info.inner == InvalidType) {
    return failResult();
  }
  if (!isLowerableValue(info.inner, analyzer)) {
    return failResult();
  }
  std::string storage = types.storageType(vecType);
  if (storage.empty()) {
    return failResult();
  }
  std::string temp = builder.ensureTemp(storage);
  builder.store("ptr", "null", vecDataPtr(temp, ctx));
  builder.store("i32", "0", vecLenPtr(temp, ctx));
  builder.store("i32", "0", vecCapPtr(temp, ctx));
  return valueResult(builder.load(storage, temp));
}
ir::LowerResult lowerVecIsEmpty(const std::string &vecPtr, LowerContext &ctx) {
  std::string len = ctx.builder.load("i32", vecLenPtr(vecPtr, ctx));
  return valueResult(ctx.builder.icmp("eq", "i32", len, "0"));
}
ir::LowerResult lowerVecPush(const std::string &vecPtr, TypeId elemType,
                             const std::string &value, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  const ir::TypeMapper &types = ctx.types;

  uint32_t elemSize = analyzer.typeSize(elemType);
  uint32_t elemAlign = analyzer.typeAlign(elemType);
  std::string dataAddr = vecDataPtr(vecPtr, ctx);
  std::string lenAddr = vecLenPtr(vecPtr, ctx);
  std::string capAddr = vecCapPtr(vecPtr, ctx);
  std::string len = builder.load("i32", lenAddr);
  std::string cap = builder.load("i32", capAddr);
  std::string hasRoom = builder.icmp("ult", "i32", len, cap);
  std::string growLabel = builder.newLabel("vec.push.grow");
  std::string doubleLabel = builder.newLabel("vec.push.double");
  std::string allocLabel = builder.newLabel("vec.push.alloc");
  std::string copyLabel = builder.newLabel("vec.push.copy");
  std::string updateLabel = builder.newLabel("vec.push.update");
  std::string readyLabel = builder.newLabel("vec.push.ready");
  builder.condBranch(hasRoom, readyLabel, growLabel);
  builder.startBlock(growLabel);
  std::string newCapSlot = builder.ensureTemp("i32");
  builder.store("i32", "4", newCapSlot);
  std::string empty = builder.icmp("eq", "i32", cap, "0");
  builder.condBranch(empty, allocLabel, doubleLabel);
  builder.startBlock(doubleLabel);
  std::string doubledCap = builder.binary("mul", "i32", cap, "2");
  builder.store("i32", doubledCap, newCapSlot);
  builder.branch(allocLabel);
  builder.startBlock(allocLabel);
  std::string newCap = builder.load("i32", newCapSlot);
  std::string bytes =
      builder.binary("mul", "i32", newCap, std::to_string(elemSize));
  std::string newData =
      builder.call("ptr", "__rx_alloc",
                   {"i32 " + bytes, "i32 " + std::to_string(elemAlign)});
  std::string hasOld = builder.icmp("ne", "i32", len, "0");
  builder.condBranch(hasOld, copyLabel, updateLabel);
  builder.startBlock(copyLabel);
  std::string data = builder.load("ptr", dataAddr);
  std::string oldBytes =
      builder.binary("mul", "i32", len, std::to_string(elemSize));
  builder.call("ptr", "memmove",
               {"ptr " + newData, "ptr " + data, "i32 " + oldBytes});
  builder.branch(updateLabel);
  builder.startBlock(updateLabel);
  builder.store("ptr", newData, dataAddr);
  builder.store("i32", newCap, capAddr);
  builder.branch(readyLabel);
  builder.startBlock(readyLabel);
  std::string finalData = builder.load("ptr", dataAddr);
  std::string elemPtr =
      builder.gep(types.storageType(elemType), finalData, {"i32 " + len});
  builder.store(types.storageType(elemType),
                toStorageValue(ctx, elemType, value), elemPtr);
  std::string newLen = builder.binary("add", "i32", len, "1");
  builder.store("i32", newLen, lenAddr);
  return unitResult();
}
ir::LowerResult lowerVecRemove(const std::string &vecPtr, TypeId elemType,
                               const std::string &index, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  const ir::TypeMapper &types = ctx.types;

  uint32_t elemSize = analyzer.typeSize(elemType);
  std::string dataAddr = vecDataPtr(vecPtr, ctx);
  std::string lenAddr = vecLenPtr(vecPtr, ctx);
  std::string data = builder.load("ptr", dataAddr);
  std::string len = builder.load("i32", lenAddr);
  std::string elemPtr =
      builder.gep(types.storageType(elemType), data, {"i32 " + index});
  std::string removedStorage =
      builder.load(types.storageType(elemType), elemPtr);
  std::string removed = fromStorageValue(ctx, elemType, removedStorage);
  std::string tail = builder.binary(
      "sub", "i32", builder.binary("sub", "i32", len, index), "1");
  std::string hasTail = builder.icmp("ne", "i32", tail, "0");
  std::string moveLabel = builder.newLabel("vec.remove.move");
  std::string doneLabel = builder.newLabel("vec.remove.done");
  builder.condBranch(hasTail, moveLabel, doneLabel);
  builder.startBlock(moveLabel);
  std::string srcIndex = builder.binary("add", "i32", index, "1");
  std::string srcPtr =
      builder.gep(types.storageType(elemType), data, {"i32 " + srcIndex});
  std::string bytes =
      builder.binary("mul", "i32", tail, std::to_string(elemSize));
  builder.call("ptr", "memmove",
               {"ptr " + elemPtr, "ptr " + srcPtr, "i32 " + bytes});
  builder.branch(doneLabel);
  builder.startBlock(doneLabel);
  std::string newLen = builder.binary("sub", "i32", len, "1");
  builder.store("i32", newLen, lenAddr);
  return valueResult(removed);
}
ir::LowerResult lowerVecBuiltinCall(const CallExpr &call, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  const ir::TypeMapper &types = ctx.types;

  auto *path = dynamic_cast<const PathExpr *>(call.callee.get());
  if (path == nullptr || path->segments.size() != 2) {
    return failResult();
  }
  const std::string &name = path->segments[1].name;
  if (name == "len" || name == "is_empty") {
    if (call.args.size() != 1) {
      return failResult();
    }
    TypeId vecType = strip(analyzer.exprType(*call.args[0]), analyzer);
    if (!isVecType(vecType, analyzer)) {
      return failResult();
    }
    auto vec = lowerAddress(*call.args[0], ctx);
    if (!vec.ok || vec.terminated || !vec.hasValue) {
      return failResult();
    }
    if (name == "len") {
      return valueResult(builder.load("i32", vecLenPtr(vec.value, ctx)));
    }
    return lowerVecIsEmpty(vec.value, ctx);
  }
  if (name == "push") {
    if (call.args.size() != 2) {
      return failResult();
    }
    TypeId vecType = strip(analyzer.exprType(*call.args[0]), analyzer);
    if (!isVecType(vecType, analyzer)) {
      return failResult();
    }
    TypeId elemType = analyzer.typeArena().get(vecType).inner;
    auto vec = lowerAddress(*call.args[0], ctx);
    if (!vec.ok || vec.terminated || !vec.hasValue) {
      return failResult();
    }
    auto value = lowerExprToType(*call.args[1], elemType, ctx);
    if (!value.ok) {
      return failResult();
    } else if (value.terminated) {
      return value;
    } else if (!value.hasValue) {
      return failResult();
    }
    return lowerVecPush(vec.value, elemType, value.value, ctx);
  }
  if (name == "remove") {
    if (call.args.size() != 2) {
      return failResult();
    }
    TypeId vecType = strip(analyzer.exprType(*call.args[0]), analyzer);
    if (!isVecType(vecType, analyzer)) {
      return failResult();
    }
    TypeId elemType = analyzer.typeArena().get(vecType).inner;
    auto vec = lowerAddress(*call.args[0], ctx);
    if (!vec.ok || vec.terminated || !vec.hasValue) {
      return failResult();
    }
    auto index = lowerExpr(*call.args[1], ctx);
    if (!index.ok) {
      return failResult();
    } else if (index.terminated) {
      return index;
    } else if (!index.hasValue) {
      return failResult();
    }
    return lowerVecRemove(vec.value, elemType, index.value, ctx);
  }
  return failResult();
}
ir::LowerResult lowerVecClone(TypeId vecType, const std::string &vecAddr,
                              LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const SemanticAnalyzer &analyzer = ctx.analyzer;
  const ir::TypeMapper &types = ctx.types;

  const TypeInfo &info = analyzer.typeArena().get(vecType);
  TypeId elemType = info.inner;
  uint32_t elemSize = analyzer.typeSize(elemType);
  uint32_t elemAlign = analyzer.typeAlign(elemType);
  std::string dataAddr = vecDataPtr(vecAddr, ctx);
  std::string lenAddr = vecLenPtr(vecAddr, ctx);
  std::string data = builder.load("ptr", dataAddr);
  std::string len = builder.load("i32", lenAddr);
  std::string dataSlot = builder.ensureTemp("ptr");
  builder.store("ptr", "null", dataSlot);
  std::string hasData = builder.icmp("ne", "i32", len, "0");
  std::string allocLabel = builder.newLabel("vec.clone.alloc");
  std::string loopLabel = builder.newLabel("vec.clone.loop");
  std::string bodyLabel = builder.newLabel("vec.clone.body");
  std::string buildLabel = builder.newLabel("vec.clone.build");
  builder.condBranch(hasData, allocLabel, buildLabel);
  builder.startBlock(allocLabel);
  std::string bytes =
      builder.binary("mul", "i32", len, std::to_string(elemSize));
  std::string newData =
      builder.call("ptr", "__rx_alloc",
                   {"i32 " + bytes, "i32 " + std::to_string(elemAlign)});
  builder.store("ptr", newData, dataSlot);
  std::string idxSlot = builder.ensureTemp("i32");
  builder.store("i32", "0", idxSlot);
  builder.branch(loopLabel);
  builder.startBlock(loopLabel);
  std::string idx = builder.load("i32", idxSlot);
  std::string more = builder.icmp("ult", "i32", idx, len);
  builder.condBranch(more, bodyLabel, buildLabel);
  builder.startBlock(bodyLabel);
  std::string srcPtr =
      builder.gep(types.storageType(elemType), data, {"i32 " + idx});
  std::string raw = builder.load(types.storageType(elemType), srcPtr);
  std::string elem = fromStorageValue(ctx, elemType, raw);
  auto cloned = lowerCloneValue(elemType, elem, ctx);
  if (!cloned.ok) {
    return failResult();
  } else if (cloned.terminated) {
    return cloned;
  } else if (!cloned.hasValue) {
    return failResult();
  }
  std::string dstPtr =
      builder.gep(types.storageType(elemType), newData, {"i32 " + idx});
  builder.store(types.storageType(elemType),
                toStorageValue(ctx, elemType, cloned.value), dstPtr);
  std::string nextIdx = builder.binary("add", "i32", idx, "1");
  builder.store("i32", nextIdx, idxSlot);
  builder.branch(loopLabel);
  builder.startBlock(buildLabel);
  std::string finalData = builder.load("ptr", dataSlot);
  std::string temp = builder.ensureTemp("%struct.Vec");
  builder.store("ptr", finalData, vecDataPtr(temp, ctx));
  builder.store("i32", len, vecLenPtr(temp, ctx));
  builder.store("i32", len, vecCapPtr(temp, ctx));
  return valueResult(builder.load("%struct.Vec", temp));
}
ir::LowerResult lowerCloneValue(TypeId type, const std::string &value,
                                LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  const TypeInfo &info = analyzer.typeArena().get(type);
  auto kind = info.kind;
  if (isScalarValue(type, analyzer) || kind == TypeKind::Ref) {
    return valueResult(value);
  } else if (kind == TypeKind::Unit) {
    return unitResult();
  } else if (kind == TypeKind::Never) {
    return failResult();
  } else if (kind == TypeKind::Box) {
    TypeId inner = info.inner;
    if (inner == InvalidType) {
      return failResult();
    }
    std::string innerStorage = types.storageType(inner);
    std::string raw = builder.load(innerStorage, value);
    std::string innerValue = fromStorageValue(ctx, inner, raw);
    auto cloned = lowerCloneValue(inner, innerValue, ctx);
    if (!cloned.ok) {
      return failResult();
    } else if (cloned.terminated) {
      return cloned;
    } else if (!cloned.hasValue) {
      return failResult();
    }
    uint32_t size = analyzer.typeSize(inner);
    uint32_t align = analyzer.typeAlign(inner);
    std::string dst = builder.call(
        "ptr", "__rx_alloc",
        {"i32 " + std::to_string(size), "i32 " + std::to_string(align)});
    builder.store(innerStorage, toStorageValue(ctx, inner, cloned.value), dst);
    return valueResult(dst);
  } else if (kind == TypeKind::Vec) {
    std::string storage = types.storageType(type);
    std::string temp = builder.ensureTemp(storage);
    builder.store(storage, value, temp);
    return lowerVecClone(type, temp, ctx);
  } else if (kind == TypeKind::Array) {
    TypeId inner = info.inner;
    if (inner == InvalidType) {
      return failResult();
    }
    std::string arrayType = types.valueType(type);
    std::string storage = types.storageType(type);
    std::string temp = builder.ensureTemp(storage);
    builder.store(storage, value, temp);
    for (uint32_t i = 0; i < info.len; ++i) {
      std::string index = std::to_string(i);
      std::string elemPtr =
          builder.gep(arrayType, temp, {"i32 0", "i32 " + index});
      std::string raw = builder.load(types.storageType(inner), elemPtr);
      std::string elem = fromStorageValue(ctx, inner, raw);
      auto cloned = lowerCloneValue(inner, elem, ctx);
      if (!cloned.ok) {
        return failResult();
      } else if (cloned.terminated) {
        return cloned;
      } else if (!cloned.hasValue) {
        return failResult();
      }
      builder.store(types.storageType(inner),
                    toStorageValue(ctx, inner, cloned.value), elemPtr);
    }
    return valueResult(builder.load(storage, temp));
  } else if (kind == TypeKind::Struct) {
    const Symbol *sym = analyzer.symbol(info.structId);
    if (sym == nullptr) {
      return failResult();
    }
    std::string storage = types.storageType(type);
    std::string temp = builder.ensureTemp(storage);
    builder.store(storage, value, temp);
    std::string fnName = "_rx_clone_" + sym->name;
    std::string result =
        builder.call(types.valueType(type), fnName, {"ptr " + temp});
    return valueResult(result);
  }
  return failResult();
}
ir::LowerResult lowerCloneStructFields(TypeId structType,
                                       const std::string &selfPtr,
                                       LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  const TypeInfo &info = analyzer.typeArena().get(structType);
  if (info.kind != TypeKind::Struct) {
    return failResult();
  }
  auto *fields = analyzer.structFields(info.structId);
  if (fields == nullptr) {
    return failResult();
  }
  std::string structIr = types.valueType(structType);
  std::string storage = types.storageType(structType);
  std::string temp = builder.ensureTemp(storage);
  for (std::size_t i = 0; i < fields->size(); ++i) {
    TypeId fieldType = (*fields)[i];
    std::string srcPtr =
        builder.gep(structIr, selfPtr, {"i32 0", "i32 " + std::to_string(i)});
    std::string raw = builder.load(types.storageType(fieldType), srcPtr);
    std::string fieldValue = fromStorageValue(ctx, fieldType, raw);
    auto cloned = lowerCloneValue(fieldType, fieldValue, ctx);
    if (!cloned.ok) {
      return failResult();
    } else if (cloned.terminated) {
      return cloned;
    } else if (!cloned.hasValue) {
      return failResult();
    }
    std::string dstPtr =
        builder.gep(structIr, temp, {"i32 0", "i32 " + std::to_string(i)});
    builder.store(types.storageType(fieldType),
                  toStorageValue(ctx, fieldType, cloned.value), dstPtr);
  }
  return valueResult(builder.load(storage, temp));
}
ir::LowerResult lowerEqualityStructFields(TypeId structType,
                                          const std::string &lhsPtr,
                                          const std::string &rhsPtr,
                                          LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  const TypeInfo &info = analyzer.typeArena().get(structType);
  if (info.kind != TypeKind::Struct) {
    return failResult();
  }
  const std::vector<TypeId> *fields = analyzer.structFields(info.structId);
  if (fields == nullptr) {
    return failResult();
  }
  std::string structIr = types.valueType(structType);
  std::string result = "true";
  for (std::size_t i = 0; i < fields->size(); ++i) {
    TypeId fieldType = (*fields)[i];
    std::string index = std::to_string(i);
    std::string lhsFieldPtr =
        builder.gep(structIr, lhsPtr, {"i32 0", "i32 " + index});
    std::string rhsFieldPtr =
        builder.gep(structIr, rhsPtr, {"i32 0", "i32 " + index});
    std::string lhsRaw =
        builder.load(types.storageType(fieldType), lhsFieldPtr);
    std::string rhsRaw =
        builder.load(types.storageType(fieldType), rhsFieldPtr);
    std::string lhsValue = fromStorageValue(ctx, fieldType, lhsRaw);
    std::string rhsValue = fromStorageValue(ctx, fieldType, rhsRaw);
    auto eq = lowerEquality(fieldType, lhsValue, rhsValue, ctx);
    if (!eq.ok || eq.terminated || !eq.hasValue) {
      return failResult();
    }
    result = builder.binary("and", "i1", result, eq.value);
  }
  return valueResult(result);
}

ir::LowerResult lowerEquality(TypeId type, const std::string &lhs,
                              const std::string &rhs, LowerContext &ctx) {
  ir::IrBuilder &builder = ctx.builder;
  const ir::TypeMapper &types = ctx.types;
  const SemanticAnalyzer &analyzer = ctx.analyzer;

  if (type == InvalidType) {
    return failResult();
  }
  const TypeInfo &info = analyzer.typeArena().get(type);
  auto kind = info.kind;
  if (isScalarInt(type, analyzer) || isScalarBool(type, analyzer)) {
    std::string irType = types.valueType(type);
    return valueResult(builder.icmp("eq", irType, lhs, rhs));
  }
  if (kind == TypeKind::Ref || kind == TypeKind::Box) {
    TypeId inner = info.inner;
    if (inner == InvalidType) {
      return failResult();
    }
    std::string innerStorage = types.storageType(inner);
    std::string lhsRaw = builder.load(innerStorage, lhs);
    std::string rhsRaw = builder.load(innerStorage, rhs);
    std::string lhsVal = fromStorageValue(ctx, inner, lhsRaw);
    std::string rhsVal = fromStorageValue(ctx, inner, rhsRaw);
    return lowerEquality(inner, lhsVal, rhsVal, ctx);
  } else if (kind == TypeKind::Struct) {
    const Symbol *sym = analyzer.symbol(info.structId);
    if (sym == nullptr) {
      return failResult();
    }
    std::string storage = types.storageType(type);
    std::string tempLhs = builder.ensureTemp(storage);
    std::string tempRhs = builder.ensureTemp(storage);
    builder.store(storage, lhs, tempLhs);
    builder.store(storage, rhs, tempRhs);
    std::string result = builder.call("i1", "_rx_eq_" + sym->name,
                                      {"ptr " + tempLhs, "ptr " + tempRhs});
    return valueResult(result);
  } else if (kind == TypeKind::Array) {
    TypeId inner = info.inner;
    if (inner == InvalidType) {
      return failResult();
    }
    std::string arrayIr = types.valueType(type);
    std::string storage = types.storageType(type);
    std::string tempLhs = builder.ensureTemp(storage);
    std::string tempRhs = builder.ensureTemp(storage);
    builder.store(storage, lhs, tempLhs);
    builder.store(storage, rhs, tempRhs);
    std::string result = "true";
    for (int i = 0; i < info.len; ++i) {
      std::string idxStr = std::to_string(i);
      std::string lhsPtr =
          builder.gep(arrayIr, tempLhs, {"i32 0", "i32 " + idxStr});
      std::string rhsPtr =
          builder.gep(arrayIr, tempRhs, {"i32 0", "i32 " + idxStr});
      std::string lhsRaw = builder.load(types.storageType(inner), lhsPtr);
      std::string rhsRaw = builder.load(types.storageType(inner), rhsPtr);
      std::string lhsVal = fromStorageValue(ctx, inner, lhsRaw);
      std::string rhsVal = fromStorageValue(ctx, inner, rhsRaw);
      auto eq = lowerEquality(inner, lhsVal, rhsVal, ctx);
      if (!eq.ok || eq.terminated || !eq.hasValue) {
        return failResult();
      }
      result = builder.binary("and", "i1", result, eq.value);
    }
    return valueResult(result);
  } else if (kind == TypeKind::Vec) {
    TypeId elemType = info.inner;
    if (elemType == InvalidType) {
      return failResult();
    }
    std::string storage = types.storageType(type);
    std::string tempLhs = builder.ensureTemp(storage);
    std::string tempRhs = builder.ensureTemp(storage);
    builder.store(storage, lhs, tempLhs);
    builder.store(storage, rhs, tempRhs);
    std::string lhsLen = builder.load("i32", vecLenPtr(tempLhs, ctx));
    std::string rhsLen = builder.load("i32", vecLenPtr(tempRhs, ctx));
    std::string lenEq = builder.icmp("eq", "i32", lhsLen, rhsLen);
    std::string resultSlot = builder.ensureTemp("i1");
    builder.store("i1", "false", resultSlot);
    std::string lensEqualLabel = builder.newLabel("vec.eq.lens");
    std::string loopLabel = builder.newLabel("vec.eq.loop");
    std::string bodyLabel = builder.newLabel("vec.eq.body");
    std::string nextLabel = builder.newLabel("vec.eq.next");
    std::string diffLabel = builder.newLabel("vec.eq.diff");
    std::string doneLabel = builder.newLabel("vec.eq.done");
    builder.condBranch(lenEq, lensEqualLabel, doneLabel);
    builder.startBlock(lensEqualLabel);
    builder.store("i1", "true", resultSlot);
    std::string idxSlot = builder.ensureTemp("i32");
    builder.store("i32", "0", idxSlot);
    builder.branch(loopLabel);
    builder.startBlock(loopLabel);
    std::string idx = builder.load("i32", idxSlot);
    std::string more = builder.icmp("ult", "i32", idx, lhsLen);
    builder.condBranch(more, bodyLabel, doneLabel);
    builder.startBlock(bodyLabel);
    std::string lhsData = builder.load("ptr", vecDataPtr(tempLhs, ctx));
    std::string rhsData = builder.load("ptr", vecDataPtr(tempRhs, ctx));
    std::string lhsPtr =
        builder.gep(types.storageType(elemType), lhsData, {"i32 " + idx});
    std::string rhsPtr =
        builder.gep(types.storageType(elemType), rhsData, {"i32 " + idx});
    std::string lhsRaw = builder.load(types.storageType(elemType), lhsPtr);
    std::string rhsRaw = builder.load(types.storageType(elemType), rhsPtr);
    std::string lhsVal = fromStorageValue(ctx, elemType, lhsRaw);
    std::string rhsVal = fromStorageValue(ctx, elemType, rhsRaw);
    auto eq = lowerEquality(elemType, lhsVal, rhsVal, ctx);
    if (!eq.ok || eq.terminated || !eq.hasValue) {
      return failResult();
    }
    builder.condBranch(eq.value, nextLabel, diffLabel);
    builder.startBlock(diffLabel);
    builder.store("i1", "false", resultSlot);
    builder.branch(doneLabel);
    builder.startBlock(nextLabel);
    std::string nextIdx = builder.binary("add", "i32", idx, "1");
    builder.store("i32", nextIdx, idxSlot);
    builder.branch(loopLabel);
    builder.startBlock(doneLabel);
    return valueResult(builder.load("i1", resultSlot));
  }
  return failResult();
}
ir::LowerResult lowerExprToType(const Expr &expr, TypeId expectedType,
                                LowerContext &ctx) {
  if (auto *block = dynamic_cast<const BlockExpr *>(&expr)) {
    return lowerBlock(*block, ctx, expectedType);
  }
  auto value = lowerExpr(expr, ctx);
  return coerceValueToType(value, ctx.exprType(expr), expectedType, ctx);
}
ir::LowerResult lowerReceiverValueToType(const Expr &expr, TypeId selfType,
                                         LowerContext &ctx) {
  auto value = lowerExpr(expr, ctx);
  if (!value.ok || value.terminated) {
    return value;
  }
  if (!value.hasValue) {
    return failResult();
  }
  TypeId current = ctx.exprType(expr);
  while (current != selfType) {
    if (!isRefOrBoxType(current, ctx.analyzer)) {
      return failResult();
    }
    TypeId inner = ctx.analyzer.typeArena().get(current).inner;
    if (inner == InvalidType) {
      return failResult();
    }
    std::string raw = ctx.builder.load(ctx.storageType(inner), value.value);
    value.value = fromStorageValue(ctx, inner, raw);
    current = inner;
  }
  return value;
}
ir::LowerResult coerceValueToType(ir::LowerResult value, TypeId actualType,
                                  TypeId expectedType, LowerContext &ctx) {
  if (!value.ok || value.terminated || expectedType == InvalidType) {
    return value;
  } else if (actualType == InvalidType) {
    return failResult();
  } else if (actualType == expectedType) {
    return value;
  } else if (!value.hasValue || !isRefType(actualType, ctx.analyzer) ||
             !isRefType(expectedType, ctx.analyzer)) {
    return failResult();
  }
  TypeId current = ctx.analyzer.typeArena().get(actualType).inner;
  TypeId target = ctx.analyzer.typeArena().get(expectedType).inner;
  if (target == InvalidType) {
    return failResult();
  }
  while (current != target) {
    if (current == InvalidType) {
      return failResult();
    }
    const TypeInfo &info = ctx.analyzer.typeArena().get(current);
    if (info.kind != TypeKind::Ref && info.kind != TypeKind::Box) {
      return failResult();
    }
    value.value = ctx.builder.load("ptr", value.value);
    current = info.inner;
  }
  return value;
}
} // namespace
bool IrGenerator::generate(std::ostream &out) {
  ir::Module module;
  ir::TypeMapper types(analyzer);
  module.addVecType();
  module.addDeclaration("get_i32", "i32", {});
  module.addDeclaration("print_i32", "void", {{"i32", "arg0"}});
  module.addDeclaration("println_i32", "void", {{"i32", "arg0"}});
  module.addDeclaration("__rx_alloc", "ptr",
                        {{"i32", "size"}, {"i32", "align"}});

  module.addDeclaration("memmove", "ptr",
                        {{"ptr", "dst"}, {"ptr", "src"}, {"i32", "n"}});
  std::vector<CloneFnInfo> cloneFns;
  std::vector<EqFnInfo> eqFns;
  std::vector<const FnDecl *> functions;
  for (const auto &item : crate.items) {
    if (auto *fn = dynamic_cast<const FnDecl *>(item.get())) {
      functions.push_back(fn);
    } else if (auto *impl = dynamic_cast<const ImplDecl *>(item.get())) {
      for (const auto &member : impl->members) {
        if (auto *fn = dynamic_cast<const FnDecl *>(member.get())) {
          functions.push_back(fn);
        }
      }
    } else if (auto *sd = dynamic_cast<const StructDecl *>(item.get())) {
      const Symbol *sym = analyzer.symbolOfDecl(*sd);
      if (sym == nullptr) {
        return false;
      }
      if (sd->derives.clone) {
        std::string name = "_rx_clone_" + sd->name;
        cloneFns.push_back({sd, sym->type, name, nullptr});
      }
      if (sd->derives.partialEq) {
        std::string name = "_rx_eq_" + sd->name;
        eqFns.push_back({sd, sym->type, name, nullptr});
      }
    }
  }
  module.reserveFunctions(module.functionCount() + cloneFns.size() +
                          eqFns.size());
  for (auto &info : cloneFns) {
    info.fn = &module.addFunction(info.name, types.valueType(info.type),
                                  {{"ptr", "self"}});
  }
  for (auto &info : cloneFns) {
    ir::IrBuilder builder(*info.fn);
    std::unordered_map<int, std::string> paramValues;
    std::string retType = types.valueType(info.type);
    LowerContext ctx{builder, types, analyzer, retType, paramValues};
    auto result = lowerCloneStructFields(info.type, "%self", ctx);
    if (!result.ok || result.terminated || !result.hasValue) {
      return false;
    }
    builder.ret(retType, result.value);
  }
  for (auto &info : eqFns) {
    info.fn =
        &module.addFunction(info.name, "i1", {{"ptr", "lhs"}, {"ptr", "rhs"}});
  }
  for (auto &info : eqFns) {
    ir::IrBuilder builder(*info.fn);
    std::unordered_map<int, std::string> paramValues;
    LowerContext ctx{builder, types, analyzer, "i1", paramValues};
    auto result = lowerEqualityStructFields(info.type, "%lhs", "%rhs", ctx);
    if (!result.ok || result.terminated || !result.hasValue) {
      return false;
    }
    builder.ret("i1", result.value);
  }

  const FnDecl *mainFn = nullptr;
  for (const auto &item : crate.items) {
    if (auto *sd = dynamic_cast<const StructDecl *>(item.get())) {
      const Symbol *sym = analyzer.symbolOfDecl(*sd);
      if (sym == nullptr || !types.declare(sym->type, module)) {
        return false;
      }
    }
  }
  for (const FnDecl *fn : functions) {
    if (fn->name == "main") {
      mainFn = fn;
      continue;
    }
    int symbol = analyzer.fnSymbol(*fn);
    const Symbol *sym = analyzer.symbol(symbol);
    if (sym == nullptr) {
      return false;
    }
    std::string irName;
    if (sym->owner == InvalidSymbol) {
      irName = "_rx_fn_" + fn->name;
    } else {
      const Symbol *owner = analyzer.symbol(sym->owner);
      if (owner == nullptr) {
        return false;
      }
      irName = "_rx_m_" + owner->name + "_" + fn->name;
    }
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
    std::string retType = types.valueType(res);
    if (fn->body == nullptr) {
      std::cerr << "missing function body: " << irName << "\n";
      return false;
    }
    ir::Function &outFn = module.addFunction(irName, retType, functionParams);
    ir::IrBuilder builder(outFn);
    LowerContext ctx{builder, types, analyzer, retType, paramValues};
    ctx.returnTypeId = res;
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
      TypeId paramType = (*params)[i];
      const Param &param = fn->params[i];
      if (!param.mutBinding && !isStructType(paramType, analyzer) &&
          !isArrayType(paramType, analyzer) &&
          !isVecType(paramType, analyzer)) {
        continue;
      }
      int id = analyzer.paramSymbol(fn->params[i]);
      if (id == InvalidSymbol) {
        return false;
      }
      const std::string &name = functionParams[i].name;
      std::string storage = types.storageType(paramType);
      std::string slot = builder.ensureSlot(id, "param." + name, storage);
      builder.store(storage, toStorageValue(ctx, paramType, "%" + name), slot);
    }
    auto body = lowerBlock(*fn->body, ctx, res);
    if (!body.ok) {
      std::cerr << "failed to lower function: " << irName << "\n";
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
  }
  if (mainFn == nullptr || mainFn->body == nullptr) {
    std::cerr << "missing source main body\n";
    return false;
  }
  ir::Function &outFn = module.addFunction(ir::SourceMain, "void", {});
  ir::IrBuilder builder(outFn);
  std::unordered_map<int, std::string> paramValues;
  LowerContext ctx{builder, types, analyzer, "void", paramValues};
  ctx.returnTypeId = analyzer.returnType(analyzer.fnSymbol(*mainFn));
  auto body = lowerBlock(*mainFn->body, ctx, ctx.returnTypeId);
  if (!body.ok) {
    std::cerr << "failed to lower function: " << ir::SourceMain << "\n";
    return false;
  }
  if (!body.terminated) {
    if (body.hasValue) {
      return false;
    }
    builder.retVoid();
  }
  module.addEntryWrapper();
  if (!module.print(out)) {
    std::cerr << "failed to emit a valid IR module\n";
    return false;
  }
  return true;
}
} // namespace rx
