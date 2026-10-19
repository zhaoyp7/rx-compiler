#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rx {

enum class TypeKind {
  I32,
  U32,
  Isize,
  Usize,
  Bool,
  Unit,
  Never,
  Ref,
  Array,
  Struct,
  Box,
  Vec,
};
using TypeId = int;
constexpr TypeId InvalidType = 0;
struct TypeInfo {
  TypeKind kind = TypeKind::Unit;
  TypeId inner = InvalidType;
  bool refMut = false;
  uint32_t len = 0;
  int structId = 0;

  bool operator==(const TypeInfo &other) const {
    return kind == other.kind && inner == other.inner &&
           refMut == other.refMut && len == other.len &&
           structId == other.structId;
  }
};
struct TypeInfoHash {
  std::size_t operator()(const TypeInfo &info) const;
};
struct StructLayout {
  uint32_t size = 0;
  uint32_t align = 1;
  std::vector<uint32_t> fieldOffsets;
};

class TypeArena {
public:
  TypeArena();
  void clear();
  TypeId intern(const TypeInfo &info);
  const TypeInfo &get(TypeId id) const;

private:
  std::vector<TypeInfo> types;
  std::unordered_map<TypeInfo, TypeId, TypeInfoHash> interned;
};

} // namespace rx
