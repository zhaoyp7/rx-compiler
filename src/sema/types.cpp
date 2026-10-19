#include "types.h"

namespace rx {

std::size_t TypeInfoHash::operator()(const TypeInfo &info) const {
  std::size_t h = (int)info.kind;
  h = h * 31 + (int)info.inner;
  h = h * 31 + (int)info.refMut;
  h = h * 31 + (int)info.len;
  h = h * 31 + (int)info.structId;
  return h;
}
TypeArena::TypeArena() { clear(); }
void TypeArena::clear() {
  types.clear();
  interned.clear();
  types.push_back(TypeInfo{});
}
TypeId TypeArena::intern(const TypeInfo &info) {
  auto it = interned.find(info);
  if (it != interned.end()) {
    return it->second;
  }
  types.push_back(info);
  TypeId id = (int)types.size() - 1;
  interned[info] = id;
  return id;
}
const TypeInfo &TypeArena::get(TypeId id) const {
  if (id < 0 || id >= (int)types.size()) {
    return types[0];
  }
  return types[id];
}

} // namespace rx
