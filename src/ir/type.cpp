#include "ir/type.h"

#include <string>
#include <vector>

#include "ir/ir.h"
#include "sema/analyzer.h"
#include "sema/symbols.h"
#include "sema/types.h"

namespace rx::ir {

TypeMapper::TypeMapper(const SemanticAnalyzer &analyzer) : analyzer(analyzer) {}
std::string TypeMapper::structName(int structId) const {
  if (structId == InvalidSymbol) {
    return "";
  }
  const Symbol *symbol = analyzer.symbol(structId);
  if (symbol == nullptr) {
    return "";
  }
  return "%struct." + symbol->name;
}
std::string TypeMapper::valueType(TypeId id) const {
  if (id == InvalidType) {
    return "";
  }
  const TypeInfo &info = analyzer.typeArena().get(id);
  const auto &kind = info.kind;
  if (kind == TypeKind::I32 || kind == TypeKind::U32 ||
      kind == TypeKind::Isize || kind == TypeKind::Usize) {
    return "i32";
  } else if (kind == TypeKind::Bool) {
    return "i1";
  } else if (kind == TypeKind::Unit || kind == TypeKind::Never) {
    return "void";
  } else if (kind == TypeKind::Ref || kind == TypeKind::Box) {
    return "ptr";
  } else if (kind == TypeKind::Struct) {
    return structName(info.structId);
  } else if (kind == TypeKind::Vec) {
    return "%struct.Vec";
  } else if (kind == TypeKind::Array) {
    return "[" + std::to_string(info.len) + " x " + storageType(info.inner) +
           "]";
  }
  return "";
}
std::string TypeMapper::storageType(TypeId id) const {
  if (id == InvalidType) {
    return "";
  }
  if (analyzer.typeArena().get(id).kind == TypeKind::Bool) {
    return "i8";
  }
  return valueType(id);
}
bool TypeMapper::declare(TypeId id, Module &module) const {
  if (id == InvalidType) {
    return false;
  }
  const TypeInfo &info = analyzer.typeArena().get(id);
  const auto &kind = info.kind;
  if (kind == TypeKind::Ref || kind == TypeKind::Box) {
    return true;
  } else if (kind == TypeKind::Array) {
    return declare(info.inner, module);
  } else if (kind == TypeKind::Vec) {
    module.addVecType();
    return true;
  } else if (kind == TypeKind::Struct) {
    std::string name = structName(info.structId);
    if (name == "") {
      return false;
    } else if (module.hasType(name)) {
      return true;
    }
    auto *fields = analyzer.structFields(info.structId);
    if (fields == nullptr) {
      return false;
    }
    std::vector<std::string> fieldTypes;
    for (TypeId field : *fields) {
      if (!declare(field, module)) {
        return false;
      }
      fieldTypes.push_back(storageType(field));
    }
    module.addStructType(name, fieldTypes);
    return true;
  }
  return true;
}

} // namespace rx::ir