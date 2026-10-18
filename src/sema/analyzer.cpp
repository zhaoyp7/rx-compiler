#include "analyzer.h"
#include "ast/ast.h"
#include "sema/symbols.h"
#include "sema/types.h"
#include <cstdint>
#include <map>
#include <unordered_set>


namespace rx {
bool SemanticAnalyzer::run(const Crate &crate) {
  symbols.clear();
  typeTable.clear();
  valueTable.clear();
  assocTable.clear();
  scopes.clear();
  bindings.clear();
  implTargets.clear();
  fnParamTypes.clear();
  fnReturnTypes.clear();
  fnReceiverTypes.clear();
  fnSymbols.clear();
  derivedCloneSymbols.clear();
  constValues.clear();
  constState.clear();
  types.clear();
  structFieldTypes.clear();
  structFieldMap.clear();
  structLayouts.clear();
  layoutState.clear();
  exprTypes.clear();
  currentReturnType = InvalidType;
  loops.clear();
  currentStruct = InvalidSymbol;
  inReceiverMethod = false;
  failed = false;
  symbols.push_back(Symbol{});
  registerBuiltins();
  registerBuiltinSignatures();
  collectCrate(crate);
  resolveCrate(crate);
  checkLayouts();
  checkDerives();
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

void SemanticAnalyzer::registerBuiltinSignatures() {
  TypeId i32 = types.intern(TypeInfo{TypeKind::I32});
  TypeId unit = unitType();
  TypeId id = valueTable["get_i32"];
  fnParamTypes[id] = {};
  fnReturnTypes[id] = i32;
  fnReceiverTypes[id] = InvalidType;
  id = valueTable["print_i32"];
  fnParamTypes[id] = {i32};
  fnReturnTypes[id] = unit;
  fnReceiverTypes[id] = InvalidType;
  id = valueTable["println_i32"];
  fnParamTypes[id] = {i32};
  fnReturnTypes[id] = unit;
  fnReceiverTypes[id] = InvalidType;
}
void SemanticAnalyzer::resolveSignatures(const Crate &crate) {
  for (const auto &item : crate.items) {
    if (auto *fn = dynamic_cast<const FnDecl *>(item.get())) {
      auto it = fnSymbols.find(fn);
      if (it == fnSymbols.end()) {
        error();
        continue;
      }
      resolveSignature(*fn, it->second, InvalidSymbol);
    } else if (auto *impl = dynamic_cast<const ImplDecl *>(item.get())) {
      auto it = implTargets.find(impl);
      if (it == implTargets.end()) {
        error();
        continue;
      }
      int owner = it->second;
      for (auto &member : impl->members) {
        if (auto *fn = dynamic_cast<const FnDecl *>(member.get())) {
          auto it = fnSymbols.find(fn);
          if (it == fnSymbols.end()) {
            error();
            continue;
          }
          resolveSignature(*fn, it->second, owner);
        }
      }
    }
  }
}
void SemanticAnalyzer::resolveSignature(const FnDecl &fn, int symbolId,
                                        int owner) {
  int last_struct = currentStruct;
  currentStruct = owner;
  std::vector<TypeId> params;
  TypeId receiver = InvalidType;
  int first_value = 0;
  if (hasSelfParam(fn)) {
    if (owner == InvalidSymbol) {
      error();
      receiver = InvalidType;
    } else {
      TypeId self_type = types.intern(
          TypeInfo{TypeKind::Struct, InvalidType, false, 0, owner});
      const Param &selfparam = fn.params[0];
      if (!selfparam.byRef) {
        receiver = self_type;
      } else if (selfparam.byMutRef) {
        receiver = types.intern(TypeInfo{TypeKind::Ref, self_type, true});
      } else {
        receiver = types.intern(TypeInfo{TypeKind::Ref, self_type, false});
      }
    }
    fnReceiverTypes[symbolId] = receiver;
    params.push_back(receiver);
    first_value = 1;
  } else {
    fnReceiverTypes[symbolId] = InvalidType;
  }
  for (int i = first_value; i < (int)fn.params.size(); i++) {
    const Param &param = fn.params[i];
    if (param.kind != ParamKind::Value) {
      error();
      params.push_back(InvalidType);
      continue;
    }
    if (param.type == nullptr) {
      error();
      params.push_back(InvalidType);
    } else {
      bool ok = false;
      TypeId tmp = resolveTypeId(*param.type, ok);
      params.push_back(ok ? tmp : InvalidType);
    }
  }
  TypeId result = unitType();
  if (fn.ret != nullptr) {
    bool ok = false;
    TypeId tmp = resolveTypeId(*fn.ret, ok);
    result = ok ? tmp : InvalidType;
  }
  fnParamTypes[symbolId] = params;
  fnReturnTypes[symbolId] = result;
  currentStruct = last_struct;
}
void SemanticAnalyzer::bindSelf(const FnDecl &fn, int symbolId) {
  if (!hasSelfParam(fn)) {
    return;
  }
  TypeId receiver = InvalidType;
  auto it = fnReceiverTypes.find(symbolId);
  if (it != fnReceiverTypes.end()) {
    receiver = it->second;
  }
  const Param *selfParam = nullptr;
  for (auto &param : fn.params) {
    if (param.kind == ParamKind::Self) {
      selfParam = &param;
      break;
    }
  }
  if (selfParam == nullptr) {
    return;
  }
  TypeId self_id = addSymbol("self", SymbolKind::Param, fn.span, nullptr);
  symbols[self_id].type = receiver;
  symbols[self_id].mutableBinding = selfParam->mutBinding;
  bindLocal("self", self_id);
}
bool SemanticAnalyzer::checkCallArgs(int symbolId, const std::vector<ExprPtr> &args, size_t startParam) {
  auto it = fnParamTypes.find(symbolId);
  if (it == fnParamTypes.end()) {
    for (auto &arg : args) {
      infer(*arg, InvalidType);
    }
    return true;
  }
  const auto &params = it->second;
  bool ok = true;
  size_t expectedCount = params.size() > startParam ? params.size() - startParam : 0;
  if (args.size() != expectedCount) {
    error();
    ok = false;
  }
  size_t n = std::max(args.size(), expectedCount);
  for (size_t i = 0; i < n; i++) {
    if (i >= args.size()) continue;
    TypeId expected = (startParam + i < params.size())
                          ? params[startParam + i]
                          : InvalidType;
    TypeId type = infer(*args[i], expected);
    if (expected != InvalidType && type != InvalidType && !checkCoercion(*args[i], type, expected)) {
      error();
      ok = false;
    }
  }
  return ok;
}
int SemanticAnalyzer::findUserMethod(TypeId receiver, const std::string &name, TypeId &selectedReceiver) {
  std::vector<TypeId> candidates;
  TypeId type = receiver;
  while (type != InvalidType) {
    candidates.push_back(type);
    candidates.push_back(types.intern(TypeInfo{TypeKind::Ref, type, false}));
    candidates.push_back(types.intern(TypeInfo{TypeKind::Ref, type, true}));
    const TypeInfo &info = types.get(type);
    if (info.kind == TypeKind::Ref || info.kind == TypeKind::Box) {
      type = info.inner;
    } else {
      break;
    }
  }
  for (TypeId candidate : candidates) {
    int structId = InvalidSymbol;
    const TypeInfo &ci = types.get(candidate);
    if (ci.kind == TypeKind::Struct) {
      structId = ci.structId;
    } else if (ci.kind == TypeKind::Ref &&
               types.get(ci.inner).kind == TypeKind::Struct) {
      structId = types.get(ci.inner).structId;
    }
    if (structId == InvalidSymbol) continue;
    auto tableIt = assocTable.find(structId);
    if (tableIt != assocTable.end()) {
      auto it = tableIt->second.find(name);
      if (it != tableIt->second.end()) {
        int id = it->second;
        if (symbols[id].kind == SymbolKind::AssocFn &&
            fnReceiverTypes[id] == candidate) {
          selectedReceiver = candidate;
          return id;
        }
      }
    }
    if (name == "clone" && ci.kind == TypeKind::Ref && !ci.refMut && types.get(ci.inner).kind == TypeKind::Struct) {
      int sid = types.get(ci.inner).structId;
      const StructDecl *sd =
          dynamic_cast<const StructDecl *>(symbols[sid].decl);
      if (sd != nullptr && sd->derives.clone) {
        selectedReceiver = candidate;
        return getDerivedClone(sid);
      }
    }
  }
  return InvalidSymbol;
}
bool SemanticAnalyzer::findBuiltinClone(TypeId receiver, TypeId &result) {
  TypeId type = receiver;
  while (type != InvalidType) {
    const TypeInfo &info = types.get(type);
    if (info.kind == TypeKind::Ref && !info.refMut) {
      std::unordered_set<int> visiting;
      if (supportsClone(info.inner, visiting)) {
        result = info.inner;
        return true;
      }
    }
    std::unordered_set<int> visiting;
    if (supportsClone(type, visiting)) {
      result = type;
      return true;
    }
    if (info.kind == TypeKind::Ref || info.kind == TypeKind::Box) {
      type = info.inner;
    } else {
      break;
    }
  }
  return false;
}
bool SemanticAnalyzer::checkDerives() {
  bool allOk = true;
  for (const auto &sym : symbols) {
    if (sym.kind != SymbolKind::Struct) {
      continue;
    }
    const StructDecl *sd = dynamic_cast<const StructDecl *>(sym.decl);
    if (sd == nullptr) {
      continue;
    }
    bool ok = true;
    if (sd->derives.copy && !sd->derives.clone) {
      ok = false;
    } else if (sd->derives.eq && !sd->derives.partialEq) {
      ok = false;
    } else if (sd->derives.duplicate) {
      ok = false;
    }
    auto it = structFieldTypes.find(typeTable[sd->name]);
    if (it != structFieldTypes.end()) {
      for (TypeId ft : it->second) {
        if (sd->derives.copy && !isCopyType(ft)) {
          ok = false;
        }
        std::unordered_set<int> visiting;
        if (sd->derives.clone && !supportsClone(ft, visiting)) {
          ok = false;
        }
        visiting.clear();
        if (sd->derives.partialEq && !supportsPartialEq(ft, visiting)) {
          ok = false;
        }
        visiting.clear();
        if (sd->derives.eq && !supportsEq(ft, visiting)) {
          ok = false;
        }
      }
    }
    if (!ok) {
      allOk = false;
      error();
    }
  }
  return allOk;
}
bool SemanticAnalyzer::supportsClone(TypeId t, std::unordered_set<int> &visiting) {
  if (t == InvalidType) return true;
  const TypeInfo &info = types.get(t);
  switch (info.kind) {
    case TypeKind::I32: case TypeKind::U32: case TypeKind::Isize:
    case TypeKind::Usize: case TypeKind::Bool: case TypeKind::Unit:
    case TypeKind::Never:
      return true;
    case TypeKind::Ref:
      return !info.refMut;
    case TypeKind::Box: case TypeKind::Vec: case TypeKind::Array:
      return supportsClone(info.inner, visiting);
    case TypeKind::Struct: {
      int sid = info.structId;
      if (visiting.count(sid)) return true;
      const StructDecl *sd =
          dynamic_cast<const StructDecl *>(symbols[sid].decl);
      if (sd == nullptr || !sd->derives.clone) return false;
      visiting.insert(sid);
      bool ok = true;
      auto it = structFieldTypes.find(sid);
      if (it != structFieldTypes.end()) {
        for (TypeId ft : it->second) {
          if (!supportsClone(ft, visiting)) { ok = false; break; }
        }
      }
      visiting.erase(sid);
      return ok;
    }
  }
  return false;
}
bool SemanticAnalyzer::supportsPartialEq(TypeId t, std::unordered_set<int> &visiting) {
  if (t == InvalidType) return true;
  const TypeInfo &info = types.get(t);
  switch (info.kind) {
    case TypeKind::I32: case TypeKind::U32: case TypeKind::Isize:
    case TypeKind::Usize: case TypeKind::Bool: case TypeKind::Unit:
      return true;
    case TypeKind::Never:
      return false;
    case TypeKind::Ref:
      return supportsPartialEq(info.inner, visiting);
    case TypeKind::Box: case TypeKind::Vec: case TypeKind::Array:
      return supportsPartialEq(info.inner, visiting);
    case TypeKind::Struct: {
      int sid = info.structId;
      if (visiting.count(sid)) return true;
      const StructDecl *sd =
          dynamic_cast<const StructDecl *>(symbols[sid].decl);
      if (sd == nullptr || !sd->derives.partialEq) return false;
      visiting.insert(sid);
      bool ok = true;
      auto it = structFieldTypes.find(sid);
      if (it != structFieldTypes.end()) {
        for (TypeId ft : it->second) {
          if (!supportsPartialEq(ft, visiting)) { ok = false; break; }
        }
      }
      visiting.erase(sid);
      return ok;
    }
  }
  return false;
}
bool SemanticAnalyzer::supportsEq(TypeId t, std::unordered_set<int> &visiting) {
  if (t == InvalidType) return true;
  const TypeInfo &info = types.get(t);
  switch (info.kind) {
    case TypeKind::I32: case TypeKind::U32: case TypeKind::Isize:
    case TypeKind::Usize: case TypeKind::Bool: case TypeKind::Unit:
      return true;
    case TypeKind::Ref:
      return supportsEq(info.inner, visiting);
    case TypeKind::Box: case TypeKind::Vec: case TypeKind::Array:
      return supportsEq(info.inner, visiting);
    case TypeKind::Struct: {
      int sid = info.structId;
      if (visiting.count(sid)) return true;
      const StructDecl *sd =
          dynamic_cast<const StructDecl *>(symbols[sid].decl);
      if (sd == nullptr || !sd->derives.eq || !sd->derives.partialEq) {
        return false;
      }
      visiting.insert(sid);
      bool ok = true;
      auto it = structFieldTypes.find(sid);
      if (it != structFieldTypes.end()) {
        for (TypeId ft : it->second) {
          if (!supportsEq(ft, visiting)) { ok = false; break; }
        }
      }
      visiting.erase(sid);
      return ok;
    }
    default:
      return false;
  }
}
int SemanticAnalyzer::getDerivedClone(int structId) {
  auto it = derivedCloneSymbols.find(structId);
  if (it != derivedCloneSymbols.end()) {
    return it->second;
  }
  int id = addSymbol("clone", SymbolKind::AssocFn, SourceSpan{}, nullptr);
  symbols[id].owner = structId;
  TypeId st = types.intern(
      TypeInfo{TypeKind::Struct, InvalidType, false, 0, structId});
  TypeId rt = types.intern(TypeInfo{TypeKind::Ref, st, false});
  fnReceiverTypes[id] = rt;
  fnParamTypes[id] = {rt};
  fnReturnTypes[id] = st;
  derivedCloneSymbols[structId] = id;
  return id;
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
      int id = addSymbol(name, SymbolKind::Fn, fn->span, fn);
      valueTable[name] = id;
      fnSymbols[fn] = id;
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
          path->segments[0].kind != PathSegmentKind::Ident || path->segments.size() != 1 || !path->segments[0].genericArgs.empty()) {
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
          fnSymbols[fn] = id;
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
    if (!seg.genericArgs.empty()) {
      error();
      return InvalidSymbol;
    }
    if (seg.kind == PathSegmentKind::Ident) {
      int id = lookupValue(seg.name);
      if (id != InvalidSymbol) {
        bindings[&path] = id;
        return id;
      }
      error();
      return InvalidSymbol;
    } else if (seg.kind == PathSegmentKind::SelfValue) {
      int id = lookupValue("self");
      if (id == InvalidSymbol) {
        error();
        return InvalidSymbol;
      }
      bindings[&path] = id;
      return id;
    } else if (seg.kind == PathSegmentKind::SelfType) {
      error();
      return InvalidSymbol;
    }
  } else {
    if (path.segments.size() != 2) {
      error();
      return InvalidSymbol;
    }
    auto &first = path.segments[0];
    auto &second = path.segments[1];
    int structId = InvalidSymbol;
    if (first.kind == PathSegmentKind::Ident) {
      auto it = typeTable.find(first.name);
      if (it == typeTable.end()) {
        error();
        return InvalidSymbol;
      }
      structId = it->second;
      if (symbols[structId].kind == SymbolKind::BuiltinType) {
        if (first.name == "Box" || first.name == "Vec") {
          if (first.genericArgs.size() != 1 || !second.genericArgs.empty()) {
            error();
            return InvalidSymbol;
          } else if (second.name == "new" || second.name == "clone") {
            return InvalidSymbol;
          }
          error();
          return InvalidSymbol;
        }
        error();
        return InvalidSymbol;
      } else if (!first.genericArgs.empty() || !second.genericArgs.empty()) {
        error();
        return InvalidSymbol;
      }
    } else if (first.kind == PathSegmentKind::SelfType) {
      if (currentStruct == InvalidSymbol) {
        error();
        return InvalidSymbol;
      }
      if (!first.genericArgs.empty() || !second.genericArgs.empty()) {
        error();
        return InvalidSymbol;
      }
      structId = currentStruct;
    } else if (first.kind == PathSegmentKind::SelfValue) {
      error();
      return InvalidSymbol;
    }
    auto tableit = assocTable.find(structId);
    if (tableit != assocTable.end()) {
      auto it = tableit->second.find(second.name);
      if (it != tableit->second.end()) {
        bindings[&path] = it->second;
        return it->second;
      }
    }
    if (second.name == "clone") {
      const StructDecl *sd = dynamic_cast<const StructDecl *>(symbols[structId].decl);
      if (sd != nullptr && sd->derives.clone) {
        int id = getDerivedClone(structId);
        bindings[&path] = id;
        return id;
      }
    }
    error();
    return InvalidSymbol;
  }
  return InvalidSymbol;
}
void SemanticAnalyzer::resolveExpr(const Expr &expr) {
  infer(expr, InvalidType);
}
bool SemanticAnalyzer::resolveStmt(const Stmt &stmt) {
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
    if (expected_id != InvalidType && init != InvalidType &&
        !checkCoercion(*let->init, init, expected_id)) {
      error();
    }
    int id = addSymbol(let->name, SymbolKind::Local, stmt.span, &stmt);
    symbols[id].type = (let->type != nullptr) ? expected_id : init;
    symbols[id].mutableBinding = let->mut;
    bindLocal(let->name, id);
    return isNever(init);
  } else if (auto expr = dynamic_cast<const ExprStmt *>(&stmt)) {
    TypeId type_id = infer(*expr->expr, InvalidType);
    bool flag = false;
    if (auto tmp = dynamic_cast<const BlockExpr *>(expr->expr.get())) {
      flag = true;
    } else if (auto tmp = dynamic_cast<const IfExpr *>(expr->expr.get())) {
      flag = true;
    } else if (auto tmp = dynamic_cast<const LoopExpr *>(expr->expr.get())) {
      flag = true;
    } else if (auto tmp = dynamic_cast<const WhileExpr *>(expr->expr.get())) {
      flag = true;
    }
    if (flag && !expr->hasSemicolon && !isUnitCompatible(type_id)) {
      error();
    }
    return isNever(type_id);
  }
  return false;
}
TypeId SemanticAnalyzer::resolveBlock(const BlockExpr &block, TypeId expected) {
  bool diverged = false;
  TypeId tailType = unitType();
  pushScope();
  for (auto &stmt : block.stmts) {
    diverged |= resolveStmt(*stmt);
  }
  if (block.tail != nullptr) {
    tailType = infer(*block.tail, expected);
    if (expected != InvalidType && tailType != InvalidType &&
        !isNever(tailType) &&
        !checkCoercion(*block.tail, tailType, expected)) {
      error();
    }
  }
  popScope();
  if (diverged) {
    return neverType();
  } else {
    return (block.tail != nullptr ? tailType : unitType());
  }
}
void SemanticAnalyzer::resolveFunction(const FnDecl &fn, bool inImpl) {
  if (hasSelfParam(fn) && !inImpl) {
    error();
  }
  inReceiverMethod = hasSelfParam(fn);
  pushScope();
  int symbolId = InvalidSymbol;
  auto it = fnSymbols.find(&fn);
  if (it != fnSymbols.end()) {
    symbolId = it->second;
  }
  bindSelf(fn, symbolId);
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
  TypeId res_type = unitType();
  if (fn.ret != nullptr) {
    bool ok = false;
    TypeId tmp = resolveTypeId(*fn.ret, ok);
    if (ok) {
      res_type = tmp;
    }
  }
  TypeId vis_id = currentReturnType;
  currentReturnType = res_type;
  if (fn.body != nullptr) {
    TypeId body_type = resolveBlock(*fn.body, res_type);
    if (fn.body->tail == nullptr && body_type != InvalidType &&
        body_type != res_type && !isNever(body_type)) {
      error();
    }
  }
  currentReturnType = vis_id;
  popScope();
  inReceiverMethod = false;
}
void SemanticAnalyzer::resolveCrate(const Crate &crate) {
  for (const auto &item : crate.items) {
    if (auto *sd = dynamic_cast<const StructDecl *>(item.get())) {
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
        structFieldMap[id][field.name] = fieldType;
      }
      currentStruct = tmp_id;
    }
  }
  resolveSignatures(crate);
  for (const auto &item : crate.items) {
    if (auto *fn = dynamic_cast<const FnDecl *>(item.get())) {
      resolveFunction(*fn, false);
    } else if (auto *sd = dynamic_cast<const StructDecl *>(item.get())) {
      continue;
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
    if (path->segments.size() != 1) {
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
      } else if (!first.genericArgs.empty()) {
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
    bool flag = (name == "i32" || name == "u32" || name == "isize" || name == "usize" || name == "bool");
    if (flag && !first.genericArgs.empty()) {
      error();
      ok = false;
      return InvalidType;
    }
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
    if (!first.genericArgs.empty()) {
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

bool SemanticAnalyzer::isUnit(TypeId t) {
  if (t == InvalidType) {
    return false;
  }
  return types.get(t).kind == TypeKind::Unit;
}
bool SemanticAnalyzer::isNever(TypeId t) {
  if (t == InvalidType) {
    return false;
  }
  return types.get(t).kind == TypeKind::Never;
}
bool SemanticAnalyzer::isUnitCompatible(TypeId t) {
  return t == InvalidType || isUnit(t) || isNever(t);
}
TypeId SemanticAnalyzer::unitType() {
  return types.intern(TypeInfo{TypeKind::Unit});
}
TypeId SemanticAnalyzer::neverType() {
  return types.intern(TypeInfo{TypeKind::Never});
}
TypeId SemanticAnalyzer::joinTypes(TypeId a, TypeId b) {
  if (a == InvalidType) {
    return b;
  } else if (b == InvalidType) {
    return a;
  } else if (a == b) {
    return a;
  } else if (isNever(a)) {
    return b;
  } else if (isNever(b)) {
    return a;
  } else if (isCoercible(b, a)) {
    return a;
  } else if (isCoercible(a, b)) {
    return b;
  }
  error();
  return InvalidType;
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
    if (!isPlace(*field->receiver)) {
      return true;
    }
    return hasMutableContainerAccess(*field->receiver);
  } else if (auto *index = dynamic_cast<const IndexExpr *>(&e)) {
    if (!isPlace(*index->base)) {
      return true;
    }
    return hasMutableContainerAccess(*index->base);
  } else if (auto *unary = dynamic_cast<const UnaryExpr *>(&e)) {
    if (unary->op != UnaryOp::Deref) {
      return false;
    }
    return isMutableAccess(e);
  }
  return false;
}
bool SemanticAnalyzer::isMutableAccess(const Expr &e) {
  if (auto *path = dynamic_cast<const PathExpr *>(&e)) {
    auto it = bindings.find(path);
    if (it == bindings.end()) {
      return false;
    }
    int id = it->second;
    if (symbols[id].mutableBinding) {
      return true;
    }
    TypeId type = symbols[id].type;
    if (type != InvalidType) {
      const TypeInfo &info = types.get(type);
      if (info.kind == TypeKind::Ref && info.refMut) {
        return true;
      }
    }
    return false;
  } else if (auto *field = dynamic_cast<const FieldAccessExpr *>(&e)) {
    auto it = exprTypes.find(&e);
    if (it != exprTypes.end() && it->second != InvalidType) {
      const TypeInfo &info = types.get(it->second);
      if (info.kind == TypeKind::Ref && info.refMut && pathAllowsStoredMutRef(*field->receiver)) {
        return true;
      }
    }
    if (!isPlace(*field->receiver)) {
      return true;
    }
    return isMutableAccess(*field->receiver);
  } else if (auto *index = dynamic_cast<const IndexExpr *>(&e)) {
    auto it = exprTypes.find(index->base.get());
    if (it == exprTypes.end()) {
      return false;
    }
    TypeId id = it->second;
    id = autoDerefContainers(id);
    if (id == InvalidType) {
      return false;
    }
    const TypeInfo &info = types.get(id);
    if (info.kind == TypeKind::Vec) {
      if (!isPlace(*index->base)) {
        return true;
      }
      return hasMutableContainerAccess(*index->base);
    } else if (info.kind == TypeKind::Array) {
      const TypeInfo &inner_info = types.get(info.inner);
      if (inner_info.kind == TypeKind::Ref && inner_info.refMut && pathAllowsStoredMutRef(*index->base)) {
        return true;
      }
      if (!isPlace(*index->base)) {
        return true;
      }
      return isMutableAccess(*index->base);
    }
    return false;
  } else if (auto *unary = dynamic_cast<const UnaryExpr *>(&e)) {
    if (unary->op != UnaryOp::Deref) {
      return false;
    }
    auto it = exprTypes.find(unary->operand.get());
    if (it == exprTypes.end() || it->second == InvalidType) {
      return false;
    }
    const TypeInfo &info = types.get(it->second);
    if (info.kind == TypeKind::Ref) {
      if (!info.refMut) {
        return false;
      } else if (!isPlace(*unary->operand)) {
        return true;
      } else {
        return pathAllowsStoredMutRef(*unary->operand);
      }
    } else if (info.kind == TypeKind::Box) {
      if (!isPlace(*unary->operand)) {
        return true;
      }
      return isMutableAccess(*unary->operand);
    }
    return false;
  }
  return true;
}
bool SemanticAnalyzer::isCopyType(TypeId id) {
  if (id == InvalidType) {
    return true;
  }
  const TypeInfo &info = types.get(id);
  auto kind = info.kind;
  if (isIntType(id) || isBoolType(id) || isUnit(id)) {
    return true;
  } else if (kind == TypeKind::Ref) {
    return !info.refMut;
  } else if (kind == TypeKind::Box || kind == TypeKind::Vec) {
    return false;
  } else if (kind == TypeKind::Never) {
    return false;
  } else if (kind == TypeKind::Array) {
    return isCopyType(info.inner);
  } else if (kind == TypeKind::Struct) {
    const StructDecl *stru =
        dynamic_cast<const StructDecl *>(symbols[info.structId].decl);
    if (stru == nullptr || !stru->derives.copy || !stru->derives.clone) {
      return false;
    }
    auto it = structFieldTypes.find(info.structId);
    if (it == structFieldTypes.end()) {
      return false;
    }
    for (auto fieldtype : it->second) {
      if (!isCopyType(fieldtype)) {
        return false;
      }
    }
    return true;
  }
  return false;
}
TypeId SemanticAnalyzer::autoDerefContainers(TypeId t) {
  while (t != InvalidType) {
    const TypeInfo &info = types.get(t);
    if (info.kind == TypeKind::Ref || info.kind == TypeKind::Box) {
      t = info.inner;
    } else {
      break;
    }
  }
  return t;
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

bool SemanticAnalyzer::hasMutableContainerAccess(const Expr &base) {
  if (isMutableAccess(base)) {
    return true;
  }
  TypeId t = exprTypes[&base];
  while (t != InvalidType) {
    const TypeInfo &info = types.get(t);
    if (info.kind == TypeKind::Ref) {
      return info.refMut;
    } else if (info.kind == TypeKind::Box) {
      t = info.inner;
      continue;
    }
    break;
  }
  return false;
}
bool SemanticAnalyzer::pathAllowsStoredMutRef(const Expr &e) {
  if (auto *path = dynamic_cast<const PathExpr *>(&e)) {
    auto it = bindings.find(path);
    if (it == bindings.end()) {
      error();
      return false;
    }
    int id = it->second;
    TypeId type = symbols[id].type;
    if (type == InvalidType) {
      return true;
    }
    const TypeInfo &info = types.get(type);
    if (info.kind == TypeKind::Ref && !info.refMut) {
      return false;
    }
    return true;
  } else if (auto *field = dynamic_cast<const FieldAccessExpr *>(&e)) {
    return pathAllowsStoredMutRef(*field->receiver);
  } else if (auto *index = dynamic_cast<const IndexExpr *>(&e)) {
    auto it = exprTypes.find(index->base.get());
    if (it == exprTypes.end()) {
      error();
      return false;
    }
    TypeId type = it->second;
    type = autoDerefContainers(type);
    const TypeInfo &info = types.get(type);
    if (info.kind == TypeKind::Vec) {
      return hasMutableContainerAccess(*index->base);
    } else if (info.kind == TypeKind::Array)  {
      return pathAllowsStoredMutRef(*index->base);
    } else {
      return false;
    }
  } else if (auto *unary = dynamic_cast<const UnaryExpr *>(&e)) {
    if (unary->op != UnaryOp::Deref) {
      return false;
    }
    auto it = exprTypes.find(unary->operand.get());
    if (it == exprTypes.end()) {
      error();
      return false;
    }
    TypeId type = it->second;
    const TypeInfo &info = types.get(type);
    if (info.kind == TypeKind::Ref) {
      if (!info.refMut) {
        return false;
      }
      return pathAllowsStoredMutRef(*unary->operand);
    } else if (info.kind == TypeKind::Box) {
      return pathAllowsStoredMutRef(*unary->operand);
    } else {
      return false;
    }
  } else {
    return false;
  }
}
bool SemanticAnalyzer::isCoercible(TypeId from, TypeId to) {
  if (from == InvalidType || to == InvalidType || from == to || isNever(from)) {
    return true;
  }
  const TypeInfo &toinfo = types.get(to);
  const TypeInfo &frominfo = types.get(from);
  if (toinfo.kind != TypeKind::Ref || frominfo.kind != TypeKind::Ref) {
    return false;
  } else if (toinfo.refMut && !frominfo.refMut) {
    return false;
  }
  bool shared_ref = false;
  TypeId now = frominfo.inner;
  TypeId to_type = toinfo.inner;
  while (true) {
    if (now == to_type) {
      return ((toinfo.refMut && shared_ref) ? false : true);
    }
    const TypeInfo &nowInfo = types.get(now);
    if (nowInfo.kind == TypeKind::Ref) {
      shared_ref |= (!nowInfo.refMut);
      now = nowInfo.inner;
    } else if (nowInfo.kind == TypeKind::Box) {
      now = nowInfo.inner;
    } else {
      break;
    }
  }
  return false;
}
bool SemanticAnalyzer::checkCoercion(const Expr &expr, TypeId from, TypeId to) {
  if (!isCoercible(from, to)) {
    return false;
  } else if (from == InvalidType || to == InvalidType) {
    return true;
  }
  const TypeInfo &frominfo = types.get(from);
  const TypeInfo &toinfo = types.get(to);
  if (frominfo.kind == TypeKind::Ref && toinfo.kind == TypeKind::Ref && frominfo.refMut && toinfo.refMut && isPlace(expr) && !pathAllowsStoredMutRef(expr)) {
    error();
    return false;
  }
  return true;
}
bool SemanticAnalyzer::checkOrderingOperands(TypeId left, TypeId right) {
  if (left == InvalidType || right == InvalidType) {
    return true;
  }
  if (left == right) {
    const TypeInfo &info = types.get(left);
    if (info.kind == TypeKind::Ref) {
      return checkOrderingOperands(info.inner, info.inner);
    } else if (isIntType(left) || isBoolType(left)) {
      return true;
    } else {
      return false;
    }
  }
  const TypeInfo &lInfo = types.get(left);
  const TypeInfo &rInfo = types.get(right);
  if (lInfo.kind == TypeKind::Ref && rInfo.kind == TypeKind::Ref && !lInfo.refMut && rInfo.refMut) {
    if (lInfo.inner == rInfo.inner) {
      return checkOrderingOperands(lInfo.inner, rInfo.inner);
    }
    return false;
  } else if (lInfo.kind == TypeKind::Ref && rInfo.kind == TypeKind::Ref && lInfo.refMut && !rInfo.refMut) {
    return false;
  } else if ((lInfo.kind == TypeKind::Ref) != (rInfo.kind == TypeKind::Ref)) {
    return false;
  }
  return false;
}

TypeId SemanticAnalyzer::infer(const Expr &expr, TypeId expected) {
  TypeId res = InvalidType;
  if (auto *lit = dynamic_cast<const IntLit *>(&expr)) {
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
      TypeKind kind =
          expected != InvalidType ? types.get(expected).kind : TypeKind::Unit;
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
        bool from_int =
            from_kind == TypeKind::I32 || from_kind == TypeKind::U32 ||
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
    res = resolveBlock(*block, expected);
  } else if (auto ifex = dynamic_cast<const IfExpr *>(&expr)) {
    TypeId cond_id = infer(*ifex->cond, InvalidType);
    if (cond_id != InvalidType && !isBoolType(cond_id)) {
      error();
    }
    if (ifex->elseBranch == nullptr) {
      TypeId then_id = resolveBlock(*ifex->then, unitType());
      if (!isUnitCompatible(then_id)) {
        error();
      }
      res = unitType();
    } else {
      TypeId then_id = resolveBlock(*ifex->then, expected);
      TypeId else_id = infer(*ifex->elseBranch, expected);
      if (expected != InvalidType) {
        if (then_id != InvalidType && !isNever(then_id) &&
            !isCoercible(then_id, expected)) {
          error();
        } else if (else_id != InvalidType && !isNever(else_id) &&
                   !checkCoercion(*ifex->elseBranch, else_id, expected)) {
          error();
        }
        res = expected;
      } else {
        res = joinTypes(then_id, else_id);
      }
    }
  } else if (auto loop = dynamic_cast<const LoopExpr *>(&expr)) {
    loops.push_back(LoopContext{false, false, expected});
    TypeId body_id = resolveBlock(*loop->body, unitType());
    if (!isUnitCompatible(body_id)) {
      error();
    }
    std::vector<TypeId> breaktypes = loops.back().breakTypes;
    loops.pop_back();
    bool allNever = !breaktypes.empty();
    for (auto type : breaktypes) {
      if (!isNever(type)) {
        allNever = false;
        break;
      }
    }
    if (breaktypes.empty() || allNever) {
      res = neverType();
    } else if (expected != InvalidType) {
      for (auto type : breaktypes) {
        if (type != InvalidType && !isNever(type) &&
            !isCoercible(type, expected)) {
          error();
        }
      }
      res = expected;
    } else {
      res = InvalidType;
      for (auto tmp : breaktypes) {
        res = joinTypes(res, tmp);
      }
    }
  } else if (auto whileex = dynamic_cast<const WhileExpr *>(&expr)) {
    loops.push_back(LoopContext{false, true});
    TypeId cond_id = infer(*whileex->cond, InvalidType);
    if (cond_id != InvalidType && !isBoolType(cond_id)) {
      error();
    }
    loops.pop_back();
    loops.push_back(LoopContext{true});
    TypeId body_id = resolveBlock(*whileex->body, unitType());
    if (!isUnitCompatible(body_id)) {
      error();
    }
    loops.pop_back();
    res = unitType();
  } else if (auto breakex = dynamic_cast<const BreakExpr *>(&expr)) {
    int idx = -1;
    if (!loops.empty() && !loops.back().isBarrier) {
      idx = (int)loops.size() - 1;
    }
    if (idx == -1) {
      error();
    } else if (breakex->value != nullptr) {
      if (loops[idx].isWhile) {
        error();
      }
      TypeId type = infer(*breakex->value, loops[idx].expected);
      if (loops[idx].expected != InvalidType && type != InvalidType &&
          !isNever(type) &&
          !checkCoercion(*breakex->value, type, loops[idx].expected)) {
        error();
      }
      loops[idx].breakTypes.push_back(type);
    } else {
      loops[idx].breakTypes.push_back(unitType());
    }
    res = neverType();
  } else if (auto continueex = dynamic_cast<const ContinueExpr *>(&expr)) {
    int idx = -1;
    if (!loops.empty() && !loops.back().isBarrier) {
      idx = (int)loops.size() - 1;
    }
    if (idx == -1) {
      error();
    }
    res = neverType();
  } else if (auto returnex = dynamic_cast<const ReturnExpr *>(&expr)) {
    if (returnex->value != nullptr) {
      TypeId id = infer(*returnex->value, currentReturnType);
      if (currentReturnType != InvalidType && id != InvalidType &&
          !isNever(id) &&
          !checkCoercion(*returnex->value, id, currentReturnType)) {
        error();
      }
    } else {
      if (currentReturnType != InvalidType && !isUnit(currentReturnType)) {
        error();
      }
    }
    res = neverType();
  } else if (auto binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    TypeId l_id = infer(*binary->lhs, InvalidType);
    TypeId r_id = infer(*binary->rhs, InvalidType);
    BinaryOp op = binary->op;
    if (op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul ||
        op == BinaryOp::Div || op == BinaryOp::Rem) {
      auto tmp_l = stripSharedRef(l_id), tmp_r = stripSharedRef(r_id);
      if (tmp_l != InvalidType && !isIntType(tmp_l)) {
        error();
      } else if (tmp_r != InvalidType && !isIntType(tmp_r)) {
        error();
      } else if (tmp_l != InvalidType && tmp_r != InvalidType &&
                 tmp_l != tmp_r) {
        error();
      } else if (tmp_l != InvalidType) {
        res = tmp_l;
      } else if (tmp_r != InvalidType) {
        res = tmp_r;
      } else {
        res = InvalidType;
      }
    } else if (op == BinaryOp::BitAnd || op == BinaryOp::BitOr ||
               op == BinaryOp::BitXor) {
      auto tmp_l = stripSharedRef(l_id), tmp_r = stripSharedRef(r_id);
      if (tmp_l != InvalidType && !isIntType(tmp_l) && !isBoolType(tmp_l)) {
        error();
      } else if (tmp_r != InvalidType && !isIntType(tmp_r) &&
                 !isBoolType(tmp_r)) {
        error();
      } else if (tmp_l != InvalidType && tmp_r != InvalidType &&
                 tmp_l != tmp_r) {
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
    } else if (op == BinaryOp::Le || op == BinaryOp::Lt || op == BinaryOp::Ge ||
               op == BinaryOp::Gt) {
      if (!checkOrderingOperands(l_id, r_id)) {
        error();
      }
      res = types.intern(TypeInfo{TypeKind::Bool});
    } else if (op == BinaryOp::Eq || op == BinaryOp::Ne) {
      if (l_id != InvalidType && r_id != InvalidType) {
        if (l_id != r_id) {
          error();
        } else {
          std::unordered_set<int> visiting;
          if (!supportsPartialEq(l_id, visiting)) error();
        }
      }
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
      if (l_id != InvalidType && r_id != InvalidType && !checkCoercion(*assign->rhs, r_id, l_id)) {
        error();
      }
    } else {
      TypeId r_id = infer(*assign->rhs, InvalidType);
      if (l_id != InvalidType) {
        bool bad_lhs = false;
        AssignOp op = assign->op;
        if (op == AssignOp::AddAssign || op == AssignOp::SubAssign ||
            op == AssignOp::MulAssign || op == AssignOp::DivAssign ||
            op == AssignOp::RemAssign) {
          bad_lhs = (isIntType(l_id) ? false : true);
        } else if (op == AssignOp::BitAndAssign ||
                   op == AssignOp::BitOrAssign ||
                   op == AssignOp::BitXorAssign) {
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
            if (op == AssignOp::AddAssign || op == AssignOp::SubAssign ||
                op == AssignOp::MulAssign || op == AssignOp::DivAssign ||
                op == AssignOp::RemAssign) {
              ok = (isIntType(tmp_r) && tmp_r == l_id);
            } else if (op == AssignOp::BitAndAssign ||
                       op == AssignOp::BitOrAssign ||
                       op == AssignOp::BitXorAssign) {
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

  } else if (auto ix = dynamic_cast<const IndexExpr *>(&expr)) {
    TypeId base = autoDerefContainers(infer(*ix->base, InvalidType));
    TypeId idx = infer(*ix->index, types.intern(TypeInfo{TypeKind::Usize}));
    if (idx != InvalidType && types.get(idx).kind != TypeKind::Usize) {
      error();
    }
    if (base == InvalidType) {
      res = InvalidType;
    } else {
      const TypeInfo &info = types.get(base);
      if (info.kind == TypeKind::Array || info.kind == TypeKind::Vec) {
        res = info.inner;
      } else {
        error();
        res = InvalidType;
      }
    }
  } else if (auto call = dynamic_cast<const CallExpr *>(&expr)) {
    if (auto path = dynamic_cast<const PathExpr *>(call->callee.get())) {
      int id = resolvePath(*path);
      if (id != InvalidSymbol && !symbols[id].callable) {
        error();
      }
      bool builtin = false;
      if (path->segments.size() >= 2) {
        const PathSegment &s0 = path->segments[0];
        const PathSegment &s1 = path->segments[1];
        if (s0.kind == PathSegmentKind::Ident &&
            (s0.name == "Box" || s0.name == "Vec")) {
          if (s0.genericArgs.size() != 1) {
            error();
          } else {
            bool ok = false;
            TypeId inner = resolveTypeId(*s0.genericArgs[0], ok);
            if (ok) {
              builtin = true;
              if (s0.name == "Box" && s1.name == "new") {
                if (call->args.size() != 1) {
                  error();
                } else {
                  TypeId a = infer(*call->args[0], inner);
                  if (a != InvalidType && inner != InvalidType &&
                      !checkCoercion(*call->args[0], a, inner)) {
                    error();
                  }
                }
                res = types.intern(TypeInfo{TypeKind::Box, inner});
              } else if (s0.name == "Vec" && s1.name == "new") {
                if (!call->args.empty()) {
                  error();
                }
                res = types.intern(TypeInfo{TypeKind::Vec, inner});
              } else if (s0.name == "Vec" && s1.name == "clone") {
                TypeId vecType = types.intern(TypeInfo{TypeKind::Vec, inner});
                TypeId refInner =
                    types.intern(TypeInfo{TypeKind::Ref, vecType, false});
                std::unordered_set<int> visiting;
                if (!supportsClone(inner, visiting)) error();
                if (call->args.size() != 1) {
                  error();
                } else {
                  TypeId a = infer(*call->args[0], refInner);
                  if (a != InvalidType && !checkCoercion(*call->args[0], a, refInner)) {
                    error();
                  }
                }
                res = vecType;
              } else if (s0.name == "Box" && s1.name == "clone") {
                TypeId boxType = types.intern(TypeInfo{TypeKind::Box, inner});
                TypeId refInner =
                    types.intern(TypeInfo{TypeKind::Ref, boxType, false});
                std::unordered_set<int> visiting;
                if (!supportsClone(inner, visiting)) error();
                if (call->args.size() != 1) {
                  error();
                } else {
                  TypeId a = infer(*call->args[0], refInner);
                  if (a != InvalidType && !checkCoercion(*call->args[0], a, refInner)) {
                    error();
                  }
                }
                res = boxType;
              } else {
                builtin = false;
              }
            }
          }
        }
      }
      if (!builtin) {
        auto it = fnParamTypes.find(id);
        if (id != InvalidSymbol && it != fnParamTypes.end()) {
          checkCallArgs(id, call->args);
          auto ret = fnReturnTypes.find(id);
          res = (ret != fnReturnTypes.end()) ? ret->second : InvalidType;
        } else {
          for (auto &arg : call->args) {
            infer(*arg, InvalidType);
          }
          res = InvalidType;
        }
      }
    } else {
      infer(*call->callee, InvalidType);
      for (auto &arg : call->args) {
        infer(*arg, InvalidType);
      }
    }
  } else if (auto array = dynamic_cast<const ArrayLit *>(&expr)) {
    bool array_expected = (expected != InvalidType &&
                           types.get(expected).kind == TypeKind::Array);
    TypeId elem_expected = InvalidType, elem_type = InvalidType;
    int len = 0;
    if (array_expected) {
      elem_expected = types.get(expected).inner;
      len = types.get(expected).len;
    }
    for (auto &elem : array->elements) {
      TypeId type = infer(*elem, elem_expected);
      if (array_expected) {
        if (type != InvalidType && elem_expected != InvalidType &&
            !checkCoercion(*elem, type, elem_expected)) {
          error();
        }
      } else {
        elem_type = joinTypes(elem_type, type);
      }
    }
    if (array_expected && array->elements.size() != len) {
      error();
    }
    res = types.intern(TypeInfo{TypeKind::Array,
                                array_expected ? elem_expected : elem_type,
                                false, (uint32_t)array->elements.size()});
  } else if (auto array = dynamic_cast<const ArrayRepeat *>(&expr)) {
    bool array_expected = (expected != InvalidType &&
                           types.get(expected).kind == TypeKind::Array);
    TypeId elem_expected = InvalidType;
    if (array_expected) {
      elem_expected = types.get(expected).inner;
    }
    TypeId elem_type = infer(*array->elem, elem_expected);
    if (array_expected && elem_type != InvalidType &&
        elem_expected != InvalidType &&
        !checkCoercion(*array->elem, elem_type, elem_expected)) {
      error();
    }
    ConstVal val;
    if (!calcConstValue(*array->count, ConstType::Usize, val)) {
      res = InvalidType;
    } else {
      uint32_t len = val.value;
      if (len > 1 && !isCopyType(elem_type)) {
        error();
      }
      if (array_expected && len != types.get(expected).len) {
        error();
      }
      TypeId resultElem = array_expected ? elem_expected : elem_type;
      res = types.intern(TypeInfo{TypeKind::Array, resultElem, false, len});
    }
  } else if (auto field = dynamic_cast<const FieldAccessExpr *>(&expr)) {
    TypeId receiver_type = infer(*field->receiver, InvalidType);
    receiver_type = autoDerefContainers(receiver_type);
    if (receiver_type == InvalidType) {
      res = InvalidType;
    } else if (types.get(receiver_type).kind != TypeKind::Struct) {
      error();
      res = InvalidType;
    } else {
      int stru_id = types.get(receiver_type).structId;
      auto it = structFieldMap.find(stru_id);
      if (it == structFieldMap.end()) {
        error();
        res = InvalidType;
      } else {
        auto tmp = it->second.find(field->field);
        if (tmp == it->second.end()) {
          error();
          res = InvalidType;
        } else {
          res = tmp->second;
        }
      }
    }
  } else if (auto method = dynamic_cast<const MethodCallExpr *>(&expr)) {
    TypeId receiver = infer(*method->receiver, InvalidType);
    bool bad = false;
    if (!method->genericArgs.empty()) {
      error();
      bad = true;
    }
    if (receiver == InvalidType) {
      for (auto &arg : method->args) {
        infer(*arg, InvalidType);
      }
      res = InvalidType;
    } else if (bad) {
      for (auto &arg : method->args) {
        infer(*arg, InvalidType);
      }
      res = InvalidType;
    } else {
      TypeId selectedReceiver = InvalidType;
      int id = findUserMethod(receiver, method->method, selectedReceiver);
      if (id != InvalidSymbol) {
        const TypeInfo &sel = types.get(selectedReceiver);
        if (sel.kind == TypeKind::Ref && sel.refMut && !isMutableAccess(*method->receiver)) {
          error();
        }
        checkCallArgs(id, method->args, 1);
        auto it = fnReturnTypes.find(id);
        res = (it != fnReturnTypes.end()) ? it->second : InvalidType;
      } else {
        TypeId cloneType = InvalidType;
        if (method->method == "clone" && findBuiltinClone(receiver, cloneType)) {
          if (!method->args.empty()) {
            error();
            for (auto &arg : method->args) {
              infer(*arg, InvalidType);
            }
          }
          res = cloneType;
        } else {
          TypeId base = autoDerefContainers(receiver);
          TypeKind kind = types.get(base).kind;
          TypeId elem = InvalidType;
          if (kind == TypeKind::Array || kind == TypeKind::Vec || kind == TypeKind::Box) {
            elem = types.get(base).inner;
          }
          TypeId usizeT = types.intern(TypeInfo{TypeKind::Usize});
          bool matched = false;
          if (method->method == "len" &&
              (kind == TypeKind::Array || kind == TypeKind::Vec)) {
            if (!method->args.empty()) {
              error();
            }
            res = usizeT;
            matched = true;
          } else if (method->method == "is_empty" && kind == TypeKind::Vec) {
            if (!method->args.empty()) {
              error();
            }
            res = types.intern(TypeInfo{TypeKind::Bool});
            matched = true;
          } else if (method->method == "push" && kind == TypeKind::Vec) {
            if (!isMutableAccess(*method->receiver)) {
              error();
            } else if (method->args.size() != 1) {
              error();
            } else {
              TypeId a = infer(*method->args[0], elem);
              if (a != InvalidType && elem != InvalidType && a != elem) error();
            }
            res = unitType();
            matched = true;
          } else if (method->method == "remove" && kind == TypeKind::Vec) {
            if (!isMutableAccess(*method->receiver)) {
              error();
            } else if (method->args.size() != 1) {
              error();
            } else {
              TypeId a = infer(*method->args[0], usizeT);
              if (a != InvalidType && types.get(a).kind != TypeKind::Usize) error();
            }
            res = elem;
            matched = true;
          }
          if (!matched) {
            for (auto &arg : method->args) infer(*arg, InvalidType);
            error();
            res = InvalidType;
          }
        }
      }
    }
  } else if (auto stru = dynamic_cast<const StructLit *>(&expr)) {
    int structId = InvalidSymbol;
    if (!stru->path.empty()) {
      const PathSegment &first = stru->path[0];
      if (first.kind == PathSegmentKind::SelfType) {
        if (currentStruct != InvalidSymbol) {
          structId = currentStruct;
        }
      } else if (first.kind == PathSegmentKind::Ident) {
        auto it = typeTable.find(first.name);
        if (it != typeTable.end() &&
            symbols[it->second].kind == SymbolKind::Struct) {
          structId = it->second;
        }
      }
    }
    if (structId == InvalidSymbol) {
      error();
      res = InvalidType;
    } else {
      auto it = structFieldMap.find(structId);
      if (it == structFieldMap.end()) {
        error();
        res = InvalidType;
      } else {
        const auto &fieldmap = it->second;
        std::unordered_map<std::string, bool> visited;
        for (auto &init : stru->fields) {
          auto fit = fieldmap.find(init.name);
          if (fit == fieldmap.end()) {
            error();
            infer(*init.value, InvalidType);
            continue;
          } else if (visited.find(init.name) != visited.end()) {
            error();
          } else {
            visited[init.name] = true;
          }
          TypeId type = infer(*init.value, fit->second);
          if (type != InvalidType && fit->second != InvalidType &&
              !checkCoercion(*init.value, type, fit->second)) {
            error();
          }
        }
        for (auto &field : fieldmap) {
          if (visited.find(field.first) == visited.end()) {
            error();
          }
        }
        res = types.intern(
            TypeInfo{TypeKind::Struct, InvalidType, false, 0, structId});
      }
    }
  }
  exprTypes[&expr] = res;
  return res;
}

} // namespace rx
