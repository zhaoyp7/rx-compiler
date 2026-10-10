#include "analyzer.h"
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
  currentStruct = InvalidSymbol;
  inReceiverMethod = false;
  failed = false;
  symbols.push_back(Symbol{});
  registerBuiltins();
  collectCrate(crate);
  resolveCrate(crate);
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
  } else if (auto *ptr = dynamic_cast<const ArrayType *>(&type)) {
    if (ptr->elem != nullptr) {
      resolveType(*ptr->elem);
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
  if (auto path = dynamic_cast<const PathExpr *>(&expr)) {
    resolvePath(*path);
  } else if (auto block = dynamic_cast<const BlockExpr *>(&expr)) {
    resolveBlock(*block);
  } else if (auto cast = dynamic_cast<const CastExpr *>(&expr)) {
    resolveExpr(*cast->expr);
    resolveType(*cast->type);
  } else if (auto ifex = dynamic_cast<const IfExpr *>(&expr)) {
    resolveExpr(*ifex->cond);
    resolveBlock(*ifex->then);
    if (ifex->elseBranch != nullptr) {
      resolveExpr(*ifex->elseBranch);
    }
  } else if (auto loop = dynamic_cast<const LoopExpr *>(&expr)) {
    resolveBlock(*loop->body);
  } else if (auto whileex = dynamic_cast<const WhileExpr *>(&expr)) {
    resolveExpr(*whileex->cond);
    resolveBlock(*whileex->body);
  } else if (auto breakex = dynamic_cast<const BreakExpr *>(&expr)) {
    if (breakex->value != nullptr) {
      resolveExpr(*breakex->value);
    }
  } else if (auto returnex = dynamic_cast<const ReturnExpr *>(&expr)) {
    if (returnex->value != nullptr) {
      resolveExpr(*returnex->value);
    }
  } else if (auto binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    resolveExpr(*binary->lhs);
    resolveExpr(*binary->rhs);
  } else if (auto assign = dynamic_cast<const AssignExpr *>(&expr)) {
    resolveExpr(*assign->lhs);
    resolveExpr(*assign->rhs);
  } else if (auto unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    resolveExpr(*unary->operand);
  } else if (auto idx = dynamic_cast<const IndexExpr *>(&expr)) {
    resolveExpr(*idx->base);
    resolveExpr(*idx->index);
  } else if (auto call = dynamic_cast<const CallExpr *>(&expr)) {
    if (auto path = dynamic_cast<const PathExpr *>(call->callee.get())) {
      int id = resolvePath(*path);
      if (id != InvalidSymbol && !symbols[id].callable) {
        error();
      }
      for (auto &arg : call->args) {
        resolveExpr(*arg);
      }
    } else {
      resolveExpr(*call->callee);
      for (auto &arg : call->args) {
        resolveExpr(*arg);
      }
    }
  } else if (auto array = dynamic_cast<const ArrayLit *>(&expr)) {
    for (auto &elem : array->elements) {
      resolveExpr(*elem);
    }
  } else if (auto array = dynamic_cast<const ArrayRepeat *>(&expr)) {
    resolveExpr(*array->elem);
  } else if (auto field = dynamic_cast<const FieldAccessExpr *>(&expr)) {
    resolveExpr(*field->receiver);
  } else if (auto method = dynamic_cast<const MethodCallExpr *>(&expr)) {
    resolveExpr(*method->receiver);
    for (auto &genericarg : method->genericArgs) {
      resolveType(*genericarg);
    }
    for (auto &arg : method->args) {
      resolveExpr(*arg);
    }
  } else if (auto stru = dynamic_cast<const StructLit *>(&expr)) {
    for (auto &path : stru->path) {
      if (typeTable.find(path.name) == typeTable.end()) {
        error();
        return;
      }
      for (auto &arg : path.genericArgs) {
        resolveType(*arg);
      }
    }
    for (auto &f : stru->fields) {
      resolveExpr(*f.value);
    }
  }
}
void SemanticAnalyzer::resolveStmt(const Stmt &stmt) {
  if (auto let = dynamic_cast<const LetStmt *>(&stmt)) {
    if (let->type != nullptr) {
      resolveType(*let->type);
    }
    resolveExpr(*let->init);
    int id = addSymbol(let->name, SymbolKind::Local, stmt.span, &stmt);
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
      bindLocal(param.name, id);
    }
    if (param.type != nullptr) {
      resolveType(*param.type);
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
      for (const auto &field : sd->fields) {
        if (field.type != nullptr) {
          resolveType(*field.type);
        }
      }
    } else if (auto *cd = dynamic_cast<const ConstDecl *>(item.get())) {
      if (cd->type != nullptr) {
        resolveType(*cd->type);
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
        }
      }
      currentStruct = InvalidSymbol;
    }
  }
}

} // namespace rx
