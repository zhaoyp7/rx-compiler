#pragma once

#include <string>
#include <unordered_map>

#include "ast/ast.h"
#include "types.h"

namespace rx {

constexpr int InvalidSymbol = 0;

enum class SymbolKind {
  Struct,
  Fn,
  Const,
  BuiltinFn,
  BuiltinType,
  Local,
  Param,
  AssocConst,
  AssocFn,
};

struct Symbol {
  std::string name;
  SymbolKind kind = SymbolKind::Local;
  bool callable = false;
  SourceSpan span;
  const Node *decl = nullptr;
  int owner = InvalidSymbol;
  TypeId type = InvalidType;
  bool mutableBinding = false;
};

using Scope = std::unordered_map<std::string, int>;

} // namespace rx
