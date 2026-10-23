#pragma once

#include <string>

#include "sema/types.h"

namespace rx {
class SemanticAnalyzer;
}

namespace rx::ir {

class Module;

class TypeMapper {
public:
  explicit TypeMapper(const SemanticAnalyzer &analyzer);
  std::string valueType(TypeId id) const;
  std::string storageType(TypeId id) const;
  bool declare(TypeId id, Module &module) const;

private:
  std::string structName(int structId) const;
  const SemanticAnalyzer &analyzer;
};

} // namespace rx::ir