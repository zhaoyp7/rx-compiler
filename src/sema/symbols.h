#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "ast/ast.h"

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
};

using Scope = std::unordered_map<std::string, int>;

} // namespace rx
