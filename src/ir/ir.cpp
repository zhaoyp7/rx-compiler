#include "ir/ir.h"

#include <cstddef>
#include <utility>

namespace rx::ir {

void printParams(std::ostream &out, const std::vector<std::string> &params) {
  out << "(";
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << params[i];
  }
  out << ")";
}
void printFields(std::ostream &out, const std::vector<std::string> &fields) {
  out << "{";
  for (std::size_t i = 0; i < fields.size(); ++i) {
    out << (i == 0 ? " " : ", ");
    out << fields[i];
  }
  if (!fields.empty()) {
    out << " ";
  }
  out << "}";
}
void Instruction::print(std::ostream &out) const {
  if (kind == Kind::RET) {
    if (hasRetValue) {
      out << "  ret " << type << " " << retValue << "\n";
    } else {
      out << "  ret void\n";
    }
  } else if (kind == Kind::CALL) {
    out << "  call " << type << " @" << callee << "()\n";
  }
}
bool BasicBlock::validate() const {
  if (label.empty() || insts.empty()) {
    return false;
  }
  for (std::size_t i = 0; i < insts.size(); ++i) {
    bool flag = (i + 1 == insts.size());
    if (insts[i].isTerminator() != flag) {
      return false;
    }
  }
  return true;
}
void BasicBlock::print(std::ostream &out) const {
  out << label << ":\n";
  for (const auto &inst : insts) {
    inst.print(out);
  }
}
bool Function::validate() const {
  if (name == "" || returnType == "") {
    return false;
  } else if (isDeclaration) {
    return true;
  } else if (blocks.empty()) {
    return false;
  }
  for (const auto &block : blocks) {
    if (!block.validate()) {
      return false;
    }
  }
  return true;
}
bool Function::print(std::ostream &out) const {
  if (!validate()) {
    return false;
  }
  if (isDeclaration) {
    out << "declare " << returnType << " @" << name;
    printParams(out, paramTypes);
    out << "\n";
  } else {
    out << "define " << returnType << " @" << name;
    printParams(out, paramTypes);
    out << " {\n";
    for (const auto &block : blocks) {
      block.print(out);
    }
    out << "}\n";
  }
  return !out.fail();
}
void Module::addSourceMain() {
  Function &fn = addFunction(SourceMain, "void", {});
  BasicBlock entry;
  entry.label = "entry";
  entry.insts.push_back(Instruction::makeRetVoid());
  fn.blocks.push_back(std::move(entry));
}
void Module::addEntryWrapper() {
  Function &fn = addFunction(EntryMain, "i32", {});
  BasicBlock entry;
  entry.label = "entry";
  entry.insts.push_back(Instruction::makeCallVoid(SourceMain));
  entry.insts.push_back(Instruction::makeRetI32(0));
  fn.blocks.push_back(std::move(entry));
}
bool Module::validate() const {
  for (const auto &fn : functions) {
    if (!fn.validate()) {
      return false;
    }
  }
  return true;
}
bool Module::print(std::ostream &out) const {
  if (!validate()) {
    return false;
  }
  out << "target triple = \"riscv32-unknown-elf\"\n";
  out << "target datalayout = \"e-m:e-p:32:32-i64:64-n32-S128\"\n\n";
  for (const auto &type : types) {
    out << type.name << " = type ";
    printFields(out, type.fields);
    out << "\n";
  }
  if (!types.empty()) {
    out << "\n";
  }
  for (const auto &fn : functions) {
    if (!fn.print(out)) {
      return false;
    }
    out << "\n";
  }
  return !out.fail();
}
Function &Module::addFunction(const std::string &name, const std::string &returnType, const std::vector<std::string> &params) {
  Function fn;
  fn.name = name;
  fn.returnType = returnType;
  fn.paramTypes = params;
  functions.push_back(std::move(fn));
  return functions.back();
}
void Module::addDeclaration(const std::string &name, const std::string &returnType, const std::vector<std::string> &params) {
  Function &fn = addFunction(name, returnType, params);
  fn.isDeclaration = true;
}
bool Module::hasType(const std::string &name) const {
  for (const auto &type : types) {
    if (type.name == name) {
      return true;
    }
  }
  return false;
}
void Module::addStructType(const std::string &name, const std::vector<std::string> &fields) {
  if (hasType(name)) {
    return;
  }
  types.push_back(NamedType{name, fields});
}
void Module::addVecType() {
  addStructType("%struct.Vec", {"ptr", "i32", "i32"});
}

} // namespace rx::ir