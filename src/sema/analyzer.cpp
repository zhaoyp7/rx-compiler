#include "analyzer.h"
#include "ast/ast.h"
#include "sema/symbols.h"
#include "sema/types.h"
#include <cstdint>
#include <map>

namespace rx {
bool SemanticAnalyzer::run(const Crate &crate) {
  symbols.clear();
  typeTable.clear();
  valueTable.clear();
  assocTable.clear();
  scopes.clear();
  bindings.clear();
  implTargets.clear();
  constValues.clear();
  constState.clear();
  types.clear();
  structFieldTypes.clear();
  structLayouts.clear();
  layoutState.clear();
  exprTypes.clear();
  currentStruct = InvalidSymbol;
  inReceiverMethod = false;
  failed = false;
  symbols.push_back(Symbol{});
  registerBuiltins();
  collectCrate(crate);
  resolveCrate(crate);
  checkLayouts();
  checkEntry();
  return (failed == false);
}
int SemanticAnalyzer::addSymbol(const std::string &name, SymbolKind kind,
                                SourceSpan span, const Node *decl) {
  Symbol tmp;
  tmp.name = name;
  tmp.callable = (kind == SymbolKind::Fn || kind == SymbolKind::BuiltinFn ||
                  kind == SymbolKind::AssocFn);
  tmp.kind = kind;
  tmp.span = span;
  tmp.decl = decl;
  symbols.push_back(tmp);
  return (int)symbols.size() - 1;
}
void SemanticAnalyzer::error() { failed = true; }
void SemanticAnalyzer::pushScope() { scopes.emplace_back(); }
void SemanticAnalyzer::popScope() { scopes.pop_back(); }
void SemanticAnalyzer::registerBuiltins() {
  typeTable["Box"] =
      addSymbol("Box", SymbolKind::BuiltinType, SourceSpan{}, nullptr);
  typeTable["Vec"] =
      addSymbol("Vec", SymbolKind::BuiltinType, SourceSpan{}, nullptr);
  valueTable["get_i32"] =
      addSymbol("get_i32", SymbolKind::BuiltinFn, SourceSpan{}, nullptr);
  valueTable["print_i32"] =
      addSymbol("print_i32", SymbolKind::BuiltinFn, SourceSpan{}, nullptr);
  valueTable["println_i32"] =
      addSymbol("println_i32", SymbolKind::BuiltinFn, SourceSpan{}, nullptr);
}
bool SemanticAnalyzer::hasSelfParam(const FnDecl &fn) {
  for (auto &param : fn.params) {
    if (param.kind == ParamKind::Self) {
      return true;
    }
  }
  return false;
}
void SemanticAnalyzer::checkEntry() {
  if (valueTable.find("main") == valueTable.end()) {
    error();
    return;
  }
  int id = valueTable["main"];
  if (symbols[id].kind != SymbolKind::Fn) {
    error();
    return;
  }
  const FnDecl *fn = dynamic_cast<const FnDecl *>(symbols[id].decl);
  if (fn == nullptr || fn->params.size() != 0 || fn->hasGenericParams) {
    error();
    return;
  } else if (fn->ret != nullptr &&
             dynamic_cast<const UnitType *>(fn->ret.get()) == nullptr) {
    error();
    return;
  }
}
void SemanticAnalyzer::collectCrate(const Crate &crate) {
  for (auto &item : crate.items) {
    if (auto *fn = dynamic_cast<FnDecl *>(item.get())) {
      if (hasSelfParam(*fn) || valueTable.find(fn->name) != valueTable.end()) {
        error();
        continue;
      }
      std::string name = fn->name;
      valueTable[name] = addSymbol(name, SymbolKind::Fn, fn->span, fn);
    } else if (auto *sd = dynamic_cast<StructDecl *>(item.get())) {
      std::string name = sd->name;
      if (typeTable.find(name) != typeTable.end() || name == "i32") {
        error();
        continue;
      }
      std::map<std::string, bool> tmp;
      for (auto &field : sd->fields) {
        if (tmp.find(field.name) != tmp.end()) {
          error();
          continue;
        }
        tmp[field.name] = true;
      }
      typeTable[name] = addSymbol(name, SymbolKind::Struct, sd->span, sd);
    } else if (auto *cd = dynamic_cast<ConstDecl *>(item.get())) {
      std::string name = cd->name;
      if (valueTable.find(name) != valueTable.end()) {
        error();
        continue;
      }
      valueTable[name] = addSymbol(name, SymbolKind::Const, cd->span, cd);
    } else if (auto *impl = dynamic_cast<ImplDecl *>(item.get())) {
      auto *path = dynamic_cast<const PathType *>(impl->target.get());
      if (path == nullptr || path->segments.empty() ||
          path->segments[0].kind != PathSegmentKind::Ident) {
        error();
        continue;
      }
      const std::string &targetName = path->segments[0].name;
      auto it = typeTable.find(targetName);
      if (it == typeTable.end() ||
          symbols[it->second].kind != SymbolKind::Struct) {
        error();
        continue;
      }
      currentStruct = it->second;
      implTargets[impl] = currentStruct;
      for (auto &member : impl->members) {
        if (auto *fn = dynamic_cast<FnDecl *>(member.get())) {
          if (assocTable[currentStruct].find(fn->name) !=
              assocTable[currentStruct].end()) {
            error();
            continue;
          }
          int id = addSymbol(fn->name, SymbolKind::AssocFn, fn->span, fn);
          symbols[id].owner = currentStruct;
          assocTable[currentStruct][fn->name] = id;
        } else if (auto *cd = dynamic_cast<ConstDecl *>(member.get())) {
          if (assocTable[currentStruct].find(cd->name) !=
              assocTable[currentStruct].end()) {
            error();
            continue;
          }
          int id = addSymbol(cd->name, SymbolKind::AssocConst, cd->span, cd);
          symbols[id].owner = currentStruct;
          assocTable[currentStruct][cd->name] = id;
        }
      }
      currentStruct = InvalidSymbol;
    }
  }
}
int SemanticAnalyzer::lookupValue(const std::string &name) {
  for (int i = (int)scopes.size() - 1; i >= 0; i--) {
    auto &scope = scopes[i];
    if (scope.find(name) != scope.end()) {
      return scope[name];
    }
  }
  if (valueTable.find(name) != valueTable.end()) {
    return valueTable[name];
  }
  return InvalidSymbol;
}
void SemanticAnalyzer::bindLocal(const std::string &name, int id) {
  if (scopes.empty()) {
    error();
    return;
  }
  scopes.back()[name] = id;
}
void SemanticAnalyzer::resolveType(const TypeSyntax &type) {
  if (auto *ptr = dynamic_cast<const UnitType *>(&type)) {
    return;
  } else if (auto *ptr = dynamic_cast<const ParenType *>(&type)) {
    if (ptr->inner != nullptr) {
      resolveType(*ptr->inner);
    }
    return;
  } else if (auto *ptr = dynamic_cast<const RefType *>(&type)) {
    if (ptr->inner != nullptr) {
      resolveType(*ptr->inner);
    }
    return;
  } else if (auto *arr = dynamic_cast<const ArrayType *>(&type)) {
    if (arr->elem != nullptr) {
      resolveType(*arr->elem);
    }
    if (arr->len != nullptr) {
      calcConstLength(*arr->len);
    }
    return;
  } else if (auto *path = dynamic_cast<const PathType *>(&type)) {
    if (path->segments.empty()) {
      error();
      return;
    }
    auto &first = path->segments[0];
    if (first.kind == PathSegmentKind::SelfValue) {
      error();
      return;
    } else if (first.kind == PathSegmentKind::SelfType) {
      if (currentStruct == InvalidSymbol) {
        error();
        return;
      }
    } else {
      std::string name = first.name;
      if (name != "i32" && name != "u32" && name != "isize" &&
          name != "usize" && name != "bool" &&
          typeTable.find(name) == typeTable.end()) {
        error();
        return;
      }
      for (auto &seg : path->segments) {
        for (auto &arg : seg.genericArgs) {
          resolveType(*arg);
        }
      }
    }
  }
}
int SemanticAnalyzer::resolvePath(const PathExpr &path) {
  if (path.segments.empty()) {
    error();
    return InvalidSymbol;
  } else if (path.segments.size() == 1) {
    auto &seg = path.segments[0];
    if (seg.kind == PathSegmentKind::Ident) {
      int id = lookupValue(seg.name);
      if (id != InvalidSymbol) {
        bindings[&path] = id;
        return id;
      } else {
        error();
        return InvalidSymbol;
      }
    } else if (seg.kind == PathSegmentKind::SelfValue) {
      if (inReceiverMethod) {
        return InvalidSymbol;
      } else {
        error();
        return InvalidSymbol;
      }
    } else if (seg.kind == PathSegmentKind::SelfType) {
      error();
      return InvalidSymbol;
    }
  } else {
    auto &first = path.segments[0];
    int structId = InvalidSymbol;
    if (first.kind == PathSegmentKind::Ident) {
      auto it = typeTable.find(first.name);
      if (it == typeTable.end()) {
        error();
        return InvalidSymbol;
      }
      structId = it->second;
      // TODO LATER
      if (symbols[structId].kind == SymbolKind::BuiltinType) {
        // Box::<T>::new / Vec::<T>::new：内建构造，P6 先按 wildcard 放行，P8/P9 实现
        if (path.segments.size() == 2 && path.segments[1].name == "new") {
          return InvalidSymbol;   // 不报错、不写 bindings
        }
        error();
        return InvalidSymbol;
      }
    } else if (first.kind == PathSegmentKind::SelfType) {
      if (currentStruct == InvalidSymbol) {
        error();
        return InvalidSymbol;
      }
      structId = currentStruct;
    } else if (first.kind == PathSegmentKind::SelfValue) {
      error();
      return InvalidSymbol;
    }
    std::string name = path.segments[1].name;
    auto tableIt = assocTable.find(structId);
    if (tableIt == assocTable.end()) {
      error();
      return InvalidSymbol;
    }
    auto it = tableIt->second.find(name);
    if (it == tableIt->second.end()) {
      error();
      return InvalidSymbol;
    }
    int id = it->second;
    bindings[&path] = id;
    return id;
  }
  return InvalidSymbol;
}
void SemanticAnalyzer::resolveExpr(const Expr &expr) {
  infer(expr, InvalidType);
}
void SemanticAnalyzer::resolveStmt(const Stmt &stmt) {
  if (auto let = dynamic_cast<const LetStmt *>(&stmt)) {
    TypeId expected_id = InvalidType;
    if (let->type != nullptr) {
      bool ok = false;
      expected_id = resolveTypeId(*let->type, ok);
      if (!ok) {
        expected_id = InvalidType;
      }
    }
    TypeId init = infer(*let->init, expected_id);
    if (expected_id != InvalidType && init != InvalidType && init != expected_id) {
      error();
    }
    int id = addSymbol(let->name, SymbolKind::Local, stmt.span, &stmt);
    symbols[id].type = (let->type != nullptr) ? expected_id : init;
    symbols[id].mutableBinding = let->mut;
    bindLocal(let->name, id);
  } else if (auto expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    resolveExpr(*expr->expr);
  }
}
void SemanticAnalyzer::resolveBlock(const BlockExpr &block) {
  pushScope();
  for (auto &stmt : block.stmts) {
    resolveStmt(*stmt);
  }
  if (block.tail != nullptr) {
    resolveExpr(*block.tail);
  }
  popScope();
}
void SemanticAnalyzer::resolveFunction(const FnDecl &fn, bool inImpl) {
  if (hasSelfParam(fn) && !inImpl) {
    error();
  }
  inReceiverMethod = hasSelfParam(fn);
  pushScope();
  std::map<std::string, bool> tmp;
  for (auto &param : fn.params) {
    if (param.kind == ParamKind::Value) {
      if (tmp.find(param.name) != tmp.end()) {
        error();
        continue;
      }
      tmp[param.name] = true;
      int id = addSymbol(param.name, SymbolKind::Param, fn.span, nullptr);
      if (param.type != nullptr) {
        bool ok = false;
        TypeId tmp = resolveTypeId(*param.type, ok);
        symbols[id].type = ok ? tmp : InvalidType;
      }
      symbols[id].mutableBinding = param.mutBinding;
      bindLocal(param.name, id);
    }
  }
  if (fn.ret != nullptr) {
    resolveType(*fn.ret);
  }
  if (fn.body != nullptr) {
    resolveBlock(*fn.body);
  }
  popScope();
  inReceiverMethod = false;
}
void SemanticAnalyzer::resolveCrate(const Crate &crate) {
  for (const auto &item : crate.items) {
    if (auto *fn = dynamic_cast<const FnDecl *>(item.get())) {
      resolveFunction(*fn, false);
    } else if (auto *sd = dynamic_cast<const StructDecl *>(item.get())) {
      auto it = typeTable.find(sd->name);
      if (it == typeTable.end()) {
        continue;
      }
      int id = it->second;
      int tmp_id = currentStruct;
      currentStruct = id;
      for (const auto &field : sd->fields) {
        TypeId fieldType = InvalidType;
        if (field.type != nullptr) {
          bool ok = false;
          fieldType = resolveTypeId(*field.type, ok);
        }
        structFieldTypes[id].push_back(fieldType);
      }
      currentStruct = tmp_id;
    } else if (auto *cd = dynamic_cast<const ConstDecl *>(item.get())) {
      if (cd->type != nullptr) {
        resolveType(*cd->type);
      }
      auto it = valueTable.find(cd->name);
      if (it != valueTable.end()) {
        ConstVal val;
        calcConstSymbol(it->second, val);
      }
    } else if (auto *impl = dynamic_cast<const ImplDecl *>(item.get())) {
      auto it = implTargets.find(impl);
      if (it == implTargets.end()) {
        continue;
      }
      currentStruct = it->second;
      for (const auto &member : impl->members) {
        if (auto *fn = dynamic_cast<const FnDecl *>(member.get())) {
          resolveFunction(*fn, true);
        } else if (auto *cd = dynamic_cast<const ConstDecl *>(member.get())) {
          if (cd->type != nullptr) {
            resolveType(*cd->type);
          }
          auto table = assocTable.find(currentStruct);
          if (table != assocTable.end()) {
            auto it = table->second.find(cd->name);
            if (it != table->second.end()) {
              ConstVal v;
              calcConstSymbol(it->second, v);
            }
          }
        }
      }
      currentStruct = InvalidSymbol;
    }
  }
}

bool SemanticAnalyzer::calcConstValue(const ConstValue &cv, ConstVal &out) {
  if (auto val = dynamic_cast<const ConstInt *>(&cv)) {
    if (val->suffix == IntSuffix::None) {
      out.type = ConstType::I32;
    } else if (val->suffix == IntSuffix::I32) {
      out.type = ConstType::I32;
    } else if (val->suffix == IntSuffix::Isize) {
      out.type = ConstType::Isize;
    } else if (val->suffix == IntSuffix::U32) {
      out.type = ConstType::U32;
    } else if (val->suffix == IntSuffix::Usize) {
      out.type = ConstType::Usize;
    }
    out.value = val->value;
  } else if (auto val = dynamic_cast<const ConstBool *>(&cv)) {
    out.type = ConstType::Bool;
    out.boolean = val->value;
  } else if (auto path = dynamic_cast<const ConstPath *>(&cv)) {
    int id = resolveConstPath(*path);
    if (id == InvalidSymbol) {
      error();
      return false;
    }
    return calcConstSymbol(id, out);
  } else if (auto val = dynamic_cast<const ConstNeg *>(&cv)) {
    ConstVal op;
    if (!calcConstValue(*val->operand, op)) {
      error();
      return false;
    }
    if (op.type != ConstType::I32 && op.type != ConstType::Isize) {
      error();
      return false;
    }
    out.type = op.type;
    out.value = -op.value;
  }
  return true;
}
bool SemanticAnalyzer::calcConstValue(const ConstValue &cv, ConstType expected,
                                      ConstVal &out) {
  if (auto val = dynamic_cast<const ConstInt *>(&cv)) {
    if (val->suffix == IntSuffix::None) {
      if (expected == ConstType::Bool) {
        error();
        return false;
      }
      out.type = expected;
    } else if (val->suffix == IntSuffix::I32) {
      out.type = ConstType::I32;
    } else if (val->suffix == IntSuffix::Isize) {
      out.type = ConstType::Isize;
    } else if (val->suffix == IntSuffix::U32) {
      out.type = ConstType::U32;
    } else if (val->suffix == IntSuffix::Usize) {
      out.type = ConstType::Usize;
    }
    out.value = val->value;
  } else if (auto val = dynamic_cast<const ConstBool *>(&cv)) {
    out.type = ConstType::Bool;
    out.boolean = val->value;
  } else if (auto path = dynamic_cast<const ConstPath *>(&cv)) {
    int id = resolveConstPath(*path);
    if (id == InvalidSymbol) {
      error();
      return false;
    }
    if (!calcConstSymbol(id, out)) {
      error();
      return false;
    }
  } else if (auto val = dynamic_cast<const ConstNeg *>(&cv)) {
    ConstVal op;
    if (!calcConstValue(*val->operand, op)) {
      error();
      return false;
    }
    if (op.type != ConstType::I32 && op.type != ConstType::Isize) {
      error();
      return false;
    }
    out.type = op.type;
    out.value = -op.value;
  }
  if (out.type != expected) {
    error();
    return false;
  }
  return true;
}
bool SemanticAnalyzer::calcConstLength(const ConstValue &cv) {
  ConstVal ans;
  if (!calcConstValue(cv, ConstType::Usize, ans)) {
    return false;
  }
  if (ans.type != ConstType::Usize) {
    error();
    return false;
  }
  return true;
}
bool SemanticAnalyzer::calcConstSymbol(int id, ConstVal &out) {
  int state = constState[id];
  if (state == 1) {
    error();
    return false;
  } else if (state == 2) {
    auto it = constValues.find(id);
    if (it == constValues.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
  if (symbols[id].kind != SymbolKind::Const &&
      symbols[id].kind != SymbolKind::AssocConst) {
    error();
    constState[id] = 2;
    return false;
  }
  const ConstDecl *cd = dynamic_cast<const ConstDecl *>(symbols[id].decl);
  if (cd == nullptr) {
    error();
    constState[id] = 2;
    return false;
  }
  bool ok = true;
  ConstType declared = typeFromSyntax(*cd->type, ok);
  if (ok == false) {
    error();
    constState[id] = 2;
    return false;
  }
  constState[id] = 1;
  int tmp = currentStruct;
  currentStruct = symbols[id].owner;
  ConstVal ans;
  if (!calcConstValue(*cd->value, declared, ans)) {
    error();
    constState[id] = 2;
    currentStruct = tmp;
    return false;
  }
  currentStruct = tmp;
  constValues[id] = ans;
  constState[id] = 2;
  out = ans;
  return true;
}
int SemanticAnalyzer::resolveConstPath(const ConstPath &path) {
  if (path.names.size() != 1 && path.names.size() != 2) {
    error();
    return InvalidSymbol;
  } else if (path.names.size() == 1) {
    std::string name = path.names[0];
    int id = lookupValue(name);
    if (id == InvalidSymbol) {
      error();
      return InvalidSymbol;
    }
    if (symbols[id].kind != SymbolKind::Const &&
        symbols[id].kind != SymbolKind::AssocConst) {
      error();
      return InvalidSymbol;
    }
    return id;
  } else {
    std::string first = path.names[0];
    std::string second = path.names[1];
    int structId = InvalidSymbol;
    if (first == "Self") {
      if (currentStruct == InvalidSymbol) {
        error();
        return InvalidSymbol;
      }
      structId = currentStruct;
    } else {
      auto it = typeTable.find(first);
      if (it == typeTable.end() ||
          symbols[it->second].kind != SymbolKind::Struct) {
        error();
        return InvalidSymbol;
      }
      structId = it->second;
    }
    auto table = assocTable.find(structId);
    if (table == assocTable.end()) {
      error();
      return InvalidSymbol;
    }
    auto it = table->second.find(second);
    if (it == table->second.end() ||
        symbols[it->second].kind != SymbolKind::AssocConst) {
      error();
      return InvalidSymbol;
    }
    return it->second;
  }
}
ConstType SemanticAnalyzer::typeFromSyntax(const TypeSyntax &type, bool &ok) {
  ok = false;
  if (auto paren = dynamic_cast<const ParenType *>(&type)) {
    if (paren->inner == nullptr) {
      return ConstType::Bool;
    }
    return typeFromSyntax(*paren->inner, ok);
  } else if (auto path = dynamic_cast<const PathType *>(&type)) {
    if (path->segments.size() != 1 ||
        path->segments[0].kind != PathSegmentKind::Ident) {
      return ConstType::Bool;
    }
    std::string name = path->segments[0].name;
    if (name == "i32") {
      ok = true;
      return ConstType::I32;
    } else if (name == "u32") {
      ok = true;
      return ConstType::U32;
    } else if (name == "isize") {
      ok = true;
      return ConstType::Isize;
    } else if (name == "usize") {
      ok = true;
      return ConstType::Usize;
    } else if (name == "bool") {
      ok = true;
      return ConstType::Bool;
    }
    return ConstType::Bool;
  } else {
    return ConstType::Bool;
  }
}

TypeId SemanticAnalyzer::resolveTypeId(const TypeSyntax &type, bool &ok) {
  ok = true;
  if (auto unit = dynamic_cast<const UnitType *>(&type)) {
    return types.intern(TypeInfo{});
  } else if (auto tmp = dynamic_cast<const ParenType *>(&type)) {
    if (tmp->inner == nullptr) {
      ok = false;
      error();
      return InvalidType;
    }
    return resolveTypeId(*tmp->inner, ok);
  } else if (auto tmp = dynamic_cast<const RefType *>(&type)) {
    TypeId inner = resolveTypeId(*tmp->inner, ok);
    if (!ok) {
      return InvalidType;
    }
    return types.intern((TypeInfo){TypeKind::Ref, inner, tmp->mut});
  } else if (auto arr = dynamic_cast<const ArrayType *>(&type)) {
    TypeId elem = resolveTypeId(*arr->elem, ok);
    if (!ok) {
      return InvalidType;
    }
    ConstVal len;
    if (!calcConstValue(*arr->len, ConstType::Usize, len)) {
      ok = false;
      error();
      return InvalidType;
    }
    return types.intern(
        TypeInfo{TypeKind::Array, elem, false, (uint32_t)len.value});
  } else if (auto path = dynamic_cast<const PathType *>(&type)) {
    if (path->segments.empty()) {
      error();
      ok = false;
      return InvalidType;
    }
    const auto &first = path->segments[0];
    if (first.kind == PathSegmentKind::SelfType) {
      if (currentStruct == InvalidSymbol) {
        error();
        ok = false;
        return InvalidType;
      }
      return types.intern(
          TypeInfo{TypeKind::Struct, InvalidType, false, 0, currentStruct});
    }
    if (first.kind == PathSegmentKind::SelfValue) {
      error();
      ok = false;
      return InvalidType;
    }
    std::string name = first.name;
    if (name == "i32") {
      return types.intern(TypeInfo{TypeKind::I32});
    } else if (name == "u32") {
      return types.intern(TypeInfo{TypeKind::U32});
    } else if (name == "isize") {
      return types.intern(TypeInfo{TypeKind::Isize});
    } else if (name == "usize") {
      return types.intern(TypeInfo{TypeKind::Usize});
    } else if (name == "bool") {
      return types.intern(TypeInfo{TypeKind::Bool});
    } else if (name == "Box" || name == "Vec") {
      if (first.genericArgs.size() != 1) {
        error();
        ok = false;
        return InvalidType;
      }
      TypeId inner = resolveTypeId(*first.genericArgs[0], ok);
      if (!ok) {
        return InvalidType;
      }
      auto kind = (name == "Box" ? TypeKind::Box : TypeKind::Vec);
      return types.intern(TypeInfo{kind, inner});
    }
    auto it = typeTable.find(name);
    if (it == typeTable.end() ||
        symbols[it->second].kind != SymbolKind::Struct) {
      error();
      ok = false;
      return InvalidType;
    }
    return types.intern(
        TypeInfo{TypeKind::Struct, InvalidType, false, 0, it->second});
  }
  ok = false;
  return InvalidType;
}
std::string SemanticAnalyzer::typeToString(TypeId id) const {
  if (id == InvalidType) {
    return "<invalid>";
  }
  TypeInfo info = types.get(id);
  if (info.kind == TypeKind::Struct) {
    return symbols[info.structId].name;
  } else {
    return types.toString(id);
  }
}
bool SemanticAnalyzer::isIntType(TypeId t) {
  if (t == InvalidType) {
    return false;
  }
  TypeKind kind = types.get(t).kind;
  return kind == TypeKind::I32 || kind == TypeKind::U32 ||
         kind == TypeKind::Isize || kind == TypeKind::Usize;
}
bool SemanticAnalyzer::isSignedType(TypeId t) {
  if (t == InvalidType) {
    return false;
  }
  TypeKind kind = types.get(t).kind;
  return kind == TypeKind::I32 || kind == TypeKind::Isize;
}
bool SemanticAnalyzer::isBoolType(TypeId t) {
  if (t == InvalidType) {
    return false;
  }
  return types.get(t).kind == TypeKind::Bool;
}
TypeId SemanticAnalyzer::stripSharedRef(TypeId t) {
  if (t == InvalidType) {
    return InvalidType;
  }
  const TypeInfo &info = types.get(t);
  if (info.kind == TypeKind::Ref && !info.refMut) {
    return info.inner;
  }
  return t;
}
bool SemanticAnalyzer::typesCompatible(TypeId a, TypeId b) {
  if (a == InvalidType || b == InvalidType) {
    return true;
  }
  return a == b;
}
bool SemanticAnalyzer::isPlace(const Expr &e) {
  if (auto *path = dynamic_cast<const PathExpr *>(&e)) {
    auto it = bindings.find(path);
    if (it == bindings.end()) {
      return false;
    }
    SymbolKind kind = symbols[it->second].kind;
    return kind == SymbolKind::Local || kind == SymbolKind::Param;
  }
  if (dynamic_cast<const FieldAccessExpr *>(&e) != nullptr ||
      dynamic_cast<const IndexExpr *>(&e) != nullptr) {
    return true;
  }
  if (auto *unary = dynamic_cast<const UnaryExpr *>(&e)) {
    return unary->op == UnaryOp::Deref;
  }
  return false;
}
bool SemanticAnalyzer::isMutablePlace(const Expr &e) {
  if (auto *path = dynamic_cast<const PathExpr *>(&e)) {
    auto it = bindings.find(path);
    if (it == bindings.end()) {
      return false;
    }
    SymbolKind kind = symbols[it->second].kind;
    if (kind != SymbolKind::Local && kind != SymbolKind::Param) {
      return false;
    }
    return symbols[it->second].mutableBinding;
  }
  if (auto *field = dynamic_cast<const FieldAccessExpr *>(&e)) {
    return isMutablePlace(*field->receiver) || !isPlace(*field->receiver);
  }
  if (auto *index = dynamic_cast<const IndexExpr *>(&e)) {
    return isMutablePlace(*index->base) || !isPlace(*index->base);
  }
  if (auto *unary = dynamic_cast<const UnaryExpr *>(&e)) {
    if (unary->op != UnaryOp::Deref) {
      return false;
    }
    auto it = exprTypes.find(unary->operand.get());
    if (it == exprTypes.end() || it->second == InvalidType) {
      return true;
    }
    const TypeInfo &info = types.get(it->second);
    if (info.kind == TypeKind::Ref) {
      return info.refMut;
    }
    if (info.kind == TypeKind::Box) {
      return true;
    }
    return false;
  }
  return false;
}
int alignUp(int now, int base) { return (now + base - 1) / base * base; }
bool SemanticAnalyzer::typeSizeAlign(TypeId id, uint32_t &size,
                                     uint32_t &align) {
  if (id == InvalidType) {
    return false;
  }
  TypeInfo info = types.get(id);
  TypeKind kind = info.kind;
  if (kind == TypeKind::I32 || kind == TypeKind::Isize) {
    size = align = 4;
  } else if (kind == TypeKind::U32 || kind == TypeKind::Usize) {
    size = align = 4;
  } else if (kind == TypeKind::Bool) {
    size = align = 1;
  } else if (kind == TypeKind::Unit || kind == TypeKind::Never) {
    size = 0;
    align = 1;
  } else if (kind == TypeKind::Ref) {
    size = align = 4;
  } else if (kind == TypeKind::Array) {
    bool flag = typeSizeAlign(info.inner, size, align);
    if (!flag) {
      return false;
    }
    size *= info.len;
  } else if (kind == TypeKind::Box) {
    size = align = 4;
  } else if (kind == TypeKind::Vec) {
    size = 12;
    align = 4;
  } else if (kind == TypeKind::Struct) {
    if (!layoutStruct(info.structId)) {
      return false;
    }
    StructLayout tmp = structLayouts[info.structId];
    size = tmp.size;
    align = tmp.align;
  }
  return true;
}
bool SemanticAnalyzer::layoutStruct(int id) {
  if (layoutState[id] == 1) {
    error();
    layoutState[id] = 3;
    return false;
  } else if (layoutState[id] == 2) {
    return true;
  } else if (layoutState[id] == 3) {
    return false;
  }
  int now = 0;
  StructLayout ans;
  layoutState[id] = 1;
  std::vector<TypeId> fieldtypes = structFieldTypes[id];
  for (auto fieldtype : fieldtypes) {
    uint32_t tmp_size = 0, tmp_align = 0;
    if (!typeSizeAlign(fieldtype, tmp_size, tmp_align)) {
      error();
      layoutState[id] = 3;
      return false;
    }
    int offset = alignUp(now, tmp_align);
    ans.fieldOffsets.push_back(offset);
    now = offset + tmp_size;
    ans.align = std::max(ans.align, tmp_align);
  }
  ans.size = alignUp(now, ans.align);
  structLayouts[id] = ans;
  layoutState[id] = 2;
  return true;
}
void SemanticAnalyzer::checkLayouts() {
  for (auto symbol : symbols) {
    if (symbol.kind != SymbolKind::Struct) {
      continue;
    }
    int id = typeTable[symbol.name];
    if (layoutState[id] != 2 && !layoutStruct(id)) {
      error();
    }
  }
}

TypeId SemanticAnalyzer::infer(const Expr &expr, TypeId expected) {
  TypeId res = InvalidType;
  if (auto *lit = dynamic_cast<const IntLit *>(&expr)) {
    // 字面量选型（见下）
    // TODO
    IntSuffix suf = lit->suffix;
    if (suf == IntSuffix::I32) {
      res = types.intern(TypeInfo{TypeKind::I32});
    } else if (suf == IntSuffix::U32) {
      res = types.intern(TypeInfo{TypeKind::U32});
    } else if (suf == IntSuffix::Isize) {
      res = types.intern(TypeInfo{TypeKind::Isize});
    } else if (suf == IntSuffix::Usize) {
      res = types.intern(TypeInfo{TypeKind::Usize});
    } else {
      TypeKind kind = expected != InvalidType ? types.get(expected).kind : TypeKind::Unit;
      if (kind == TypeKind::I32 || kind == TypeKind::U32 ||
          kind == TypeKind::Isize || kind == TypeKind::Usize) {
        res = expected;
      } else {
        res = types.intern(TypeInfo{TypeKind::I32});
      }
    }
  } else if (auto *tmp = dynamic_cast<const BoolLit *>(&expr)) {
    res = types.intern(TypeInfo{TypeKind::Bool});
  } else if (auto *tmp = dynamic_cast<const UnitLit *>(&expr)) {
    res = types.intern(TypeInfo{TypeKind::Unit});
  } else if (auto path = dynamic_cast<const PathExpr *>(&expr)) {
    int id = resolvePath(*path);
    if (id != InvalidSymbol) {
      res = symbols[id].type;
    }
  } else if (auto cast = dynamic_cast<const CastExpr *>(&expr)) {
    bool ok = false;
    TypeId from = infer(*cast->expr, InvalidType);
    TypeId to = resolveTypeId(*cast->type, ok);
    if (!ok) {
      res = InvalidType;
    } else {
      bool bad = false;
      if (from != InvalidType) {
        TypeKind from_kind = types.get(from).kind;
        TypeKind to_kind = types.get(to).kind;
        bool from_int = from_kind == TypeKind::I32 || from_kind == TypeKind::U32 ||
                      from_kind == TypeKind::Isize || from_kind == TypeKind::Usize;
        bool from_bool = from_kind == TypeKind::Bool;
        bool to_int = to_kind == TypeKind::I32 || to_kind == TypeKind::U32 ||
                      to_kind == TypeKind::Isize || to_kind == TypeKind::Usize;
        if (to_kind == TypeKind::Bool || from_kind == TypeKind::Ref) {
          bad = true;
        } else if (!(from_int && to_int) && !(from_bool && to_int)) {
          bad = true;
        }
      }
      if (bad) {
        error();
      }
      res = to;      
    }
  } else if (auto block = dynamic_cast<const BlockExpr *>(&expr)) {
    resolveBlock(*block);
  } else if (auto ifex = dynamic_cast<const IfExpr *>(&expr)) {
    infer(*ifex->cond, InvalidType);
    resolveBlock(*ifex->then);
    if (ifex->elseBranch != nullptr) {
      infer(*ifex->elseBranch, InvalidType);
    }
  } else if (auto loop = dynamic_cast<const LoopExpr *>(&expr)) {
    resolveBlock(*loop->body);
  } else if (auto whileex = dynamic_cast<const WhileExpr *>(&expr)) {
    infer(*whileex->cond, InvalidType);
    resolveBlock(*whileex->body);
  } else if (auto breakex = dynamic_cast<const BreakExpr *>(&expr)) {
    if (breakex->value != nullptr) {
      infer(*breakex->value, InvalidType);
    }
  } else if (auto returnex = dynamic_cast<const ReturnExpr *>(&expr)) {
    if (returnex->value != nullptr) {
      infer(*returnex->value, InvalidType);
    }
  } else if (auto binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    TypeId l_id = infer(*binary->lhs, InvalidType);
    TypeId r_id = infer(*binary->rhs, InvalidType);
    BinaryOp op = binary->op;
    if (op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul || op == BinaryOp::Div || op == BinaryOp::Rem) {
      auto tmp_l = stripSharedRef(l_id), tmp_r = stripSharedRef(r_id);
      if (tmp_l != InvalidType && !isIntType(tmp_l)) {
        error();
      } else if (tmp_r != InvalidType && !isIntType(tmp_r)) {
        error();
      } else if (tmp_l != InvalidType && tmp_r != InvalidType && tmp_l != tmp_r) {
        error();
      } else if (tmp_l != InvalidType) {
        res = tmp_l;
      } else if (tmp_r != InvalidType) {
        res = tmp_r;
      } else {
        res = InvalidType;
      }
    } else if (op == BinaryOp::BitAnd || op == BinaryOp::BitOr || op == BinaryOp::BitXor) {
      auto tmp_l = stripSharedRef(l_id), tmp_r = stripSharedRef(r_id);
      if (tmp_l != InvalidType && !isIntType(tmp_l) && !isBoolType(tmp_l)) {
        error();
      } else if (tmp_r != InvalidType && !isIntType(tmp_r) && !isBoolType(tmp_r)) {
        error();
      } else if (tmp_l != InvalidType && tmp_r != InvalidType && tmp_l != tmp_r) {
        error();
      }
      res = (tmp_l == InvalidType ? tmp_r : tmp_l);
    } else if (op == BinaryOp::Shl || op == BinaryOp::Shr) {
      auto tmp_l = stripSharedRef(l_id), tmp_r = stripSharedRef(r_id);
      if (tmp_l != InvalidType && !isIntType(tmp_l)) {
        error();
      } else if (tmp_r != InvalidType && !isIntType(tmp_r)) {
        error();
      } else {
        res = tmp_l;
      }
    } else if (op == BinaryOp::Le || op == BinaryOp::Lt || op == BinaryOp::Ge || op == BinaryOp::Gt) {
      auto tmp_l = stripSharedRef(l_id), tmp_r = stripSharedRef(r_id);
      if (tmp_l != InvalidType && !isIntType(tmp_l) && !isBoolType(tmp_l)) {
        error();
      } else if (tmp_r != InvalidType && !isIntType(tmp_r) && !isBoolType(tmp_r)) {
        error();
      } else if (tmp_l != InvalidType && tmp_r != InvalidType && tmp_l != tmp_r) {
        error();
      }
      res = types.intern(TypeInfo{TypeKind::Bool});
    } else if (op == BinaryOp::Eq || op == BinaryOp::Ne) {
      res = types.intern(TypeInfo{TypeKind::Bool});
    } else if (op == BinaryOp::LAnd || op == BinaryOp::LOr) {
      if (l_id != InvalidType && !isBoolType(l_id)) {
        error();
      } else if (r_id != InvalidType && !isBoolType(r_id)) {
        error();
      }
      res = types.intern(TypeInfo{TypeKind::Bool});
    }
  } else if (auto assign = dynamic_cast<const AssignExpr *>(&expr)) {
    TypeId l_id = infer(*assign->lhs, InvalidType);
    if (!isPlace(*assign->lhs) || !isMutablePlace(*assign->lhs)) {
      error();
    }
    if (assign->op == AssignOp::Assign) {
      TypeId r_id = infer(*assign->rhs, l_id);
      if (l_id != InvalidType && r_id != InvalidType && l_id != r_id) {
        error();
      }
    } else {
      TypeId r_id = infer(*assign->rhs, InvalidType);
      if (l_id != InvalidType) {
        bool bad_lhs = false;
        AssignOp op = assign->op;
        if (op == AssignOp::AddAssign || op == AssignOp::SubAssign || op == AssignOp::MulAssign || op == AssignOp::DivAssign || op == AssignOp::RemAssign) {
          bad_lhs = (isIntType(l_id) ? false : true);
        } else if (op == AssignOp::BitAndAssign || op == AssignOp::BitOrAssign || op == AssignOp::BitXorAssign) {
          bad_lhs = ((isIntType(l_id) || isBoolType(l_id)) ? false : true);
        } else if (op == AssignOp::ShlAssign || op == AssignOp::ShrAssign) {
          bad_lhs = (isIntType(l_id) ? false : true);
        }
        if (bad_lhs) {
          error();
        } else {
          TypeId tmp_r = stripSharedRef(r_id);
          if (tmp_r != InvalidType) {
            bool ok = true;
            if (op == AssignOp::AddAssign || op == AssignOp::SubAssign || op == AssignOp::MulAssign || op == AssignOp::DivAssign || op == AssignOp::RemAssign) {
              ok = (isIntType(tmp_r) && tmp_r == l_id);
            } else if (op == AssignOp::BitAndAssign || op == AssignOp::BitOrAssign || op == AssignOp::BitXorAssign) {
              ok = ((isIntType(tmp_r) || isBoolType(tmp_r)) && tmp_r == l_id);
            } else if (op == AssignOp::ShlAssign || op == AssignOp::ShrAssign) {
              ok = isIntType(tmp_r);
            }
            if (!ok) {
              error();
            }
          }
        }
      }
    }
  } else if (auto unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    TypeId type_id = infer(*unary->operand, InvalidType);
    if (unary->op == UnaryOp::Neg) {
      TypeId tmp = stripSharedRef(type_id);
      if (tmp != InvalidType && !isSignedType(tmp)) {
        error();
      }
      res = tmp;
    } else if (unary->op == UnaryOp::Not) {
      TypeId tmp = stripSharedRef(type_id);
      if (tmp != InvalidType && !isIntType(tmp) && !isBoolType(tmp)) {
        error();
      }
      res = tmp;
    } else if (unary->op == UnaryOp::Ref) {
      res = types.intern(TypeInfo{TypeKind::Ref, type_id, false});
    } else if (unary->op == UnaryOp::RefMut) {
      if (isPlace(*unary->operand) && !isMutablePlace(*unary->operand)) {
        error();
      }
      res = types.intern(TypeInfo{TypeKind::Ref, type_id, true});
    } else if (unary->op == UnaryOp::Deref) {
      if (type_id == InvalidType) {
        res = InvalidType;
      } else {
        const TypeInfo &info = types.get(type_id);
        if (info.kind == TypeKind::Ref || info.kind == TypeKind::Box) {
          res = info.inner;
        } else {
          error();
          res = InvalidType;
        }
      }
    }

  } else if (auto idx = dynamic_cast<const IndexExpr *>(&expr)) {
    infer(*idx->base, InvalidType);
    infer(*idx->index, InvalidType);
  } else if (auto call = dynamic_cast<const CallExpr *>(&expr)) {
    if (auto path = dynamic_cast<const PathExpr *>(call->callee.get())) {
      int id = resolvePath(*path);
      if (id != InvalidSymbol && !symbols[id].callable) {
        error();
      }
      for (auto &arg : call->args) {
        infer(*arg, InvalidType);
      }
    } else {
      infer(*call->callee, InvalidType);
      for (auto &arg : call->args) {
        infer(*arg, InvalidType);
      }
    }
  } else if (auto array = dynamic_cast<const ArrayLit *>(&expr)) {
    for (auto &elem : array->elements) {
      infer(*elem, InvalidType);
    }
  } else if (auto array = dynamic_cast<const ArrayRepeat *>(&expr)) {
    infer(*array->elem, InvalidType);
    if (array->count != nullptr) {
      calcConstLength(*array->count);
    }
  } else if (auto field = dynamic_cast<const FieldAccessExpr *>(&expr)) {
    infer(*field->receiver, InvalidType);
  } else if (auto method = dynamic_cast<const MethodCallExpr *>(&expr)) {
    infer(*method->receiver, InvalidType);
    for (auto &genericarg : method->genericArgs) {
      resolveType(*genericarg);
    }
    for (auto &arg : method->args) {
      infer(*arg, InvalidType);
    }
  } else if (auto stru = dynamic_cast<const StructLit *>(&expr)) {
    for (auto &path : stru->path) {
      if (typeTable.find(path.name) == typeTable.end()) {
        error();
      }
      for (auto &arg : path.genericArgs) {
        resolveType(*arg);
      }
    }
    for (auto &f : stru->fields) {
      infer(*f.value, InvalidType);
    }
  }
  exprTypes[&expr] = res;
  return res;
}

} // namespace rx
