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
std::string TypeArena::toString(TypeId id) const {
  if (id < 0 || id >= (int)types.size()) {
    return "<invalid>";
  }
  TypeInfo info = get(id);
  TypeKind kind = info.kind;
  if (kind == TypeKind::I32) {
    return "i32";
  } else if (kind == TypeKind::U32) {
    return "u32";
  } else if (kind == TypeKind::Isize) {
    return "isize";
  } else if (kind == TypeKind::Usize) {
    return "usize";
  } else if (kind == TypeKind::Bool) {
    return "bool";
  } else if (kind == TypeKind::Unit) {
    return "()";
  } else if (kind == TypeKind::Never) {
    return "!";
  } else if (kind == TypeKind::Ref) {
    return (info.refMut ? "&mut " : "&") + toString(info.inner);
  } else if (kind == TypeKind::Array) {
    return "[" + toString(info.inner) + "; " + std::to_string(info.len) + "]";
  } else if (kind == TypeKind::Struct) {
    return "struct#" + std::to_string(info.structId);
  } else if (kind == TypeKind::Box) {
    return "Box<" + toString(info.inner) + ">";
  } else if (kind == TypeKind::Vec) {
    return "Vec<" + toString(info.inner) + ">";
  }
  return "<invalid>";
}

} // namespace rx
