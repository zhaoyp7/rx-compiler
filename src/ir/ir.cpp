#include "ir/ir.h"

namespace rx::ir {

void printParams(std::ostream &out, const std::vector<FunctionParam> &params,
                 bool withNames) {
  out << "(";
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << params[i].type;
    if (withNames) {
      out << " %";
      if (params[i].name.empty()) {
        out << "arg" << i;
      } else {
        out << params[i].name;
      }
    }
  }
  out << ")";
}
void printStringList(std::ostream &out, const std::vector<std::string> &items) {
  out << "(";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << items[i];
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
    out << "  ret " << type;
    if (!operands.empty()) {
      out << ' ' << operands[0];
    }
    out << '\n';
  } else if (kind == Kind::CALL) {
    out << "  ";
    if (!result.empty()) {
      out << result << " = ";
    }
    out << "call " << type << " @" << op;
    printStringList(out, operands);
    out << "\n";
  } else if (kind == Kind::ALLOCA) {
    out << "  " << result << " = alloca " << type << "\n";
  } else if (kind == Kind::LOAD) {
    out << "  " << result << " = load " << type << ", ptr " << operands[0]
        << "\n";
  } else if (kind == Kind::STORE) {
    out << "  store " << type << " " << operands[0] << ", ptr " << operands[1]
        << "\n";
  } else if (kind == Kind::BINARY) {
    out << "  " << result << " = " << op << " " << type << " " << operands[0]
        << ", " << operands[1] << "\n";
  } else if (kind == Kind::ICMP) {
    out << "  " << result << " = icmp " << op << " " << type << " "
        << operands[0] << ", " << operands[1] << "\n";
  } else if (kind == Kind::CAST) {
    out << "  " << result << " = " << op << " " << type << " " << operands[0]
        << " to " << operands[1] << "\n";
  } else if (kind == Kind::BR) {
    if (operands.size() == 1) {
      out << "  br label %" << operands[0] << "\n";
    } else {
      out << "  br i1 " << operands[0] << ", label %" << operands[1]
          << ", label %" << operands[2] << "\n";
    }
  } else if (kind == Kind::UNREACHABLE) {
    out << "  unreachable\n";
  } else if (kind == Kind::GEP) {
    out << "  " << result << " = getelementptr " << type << ", ptr "
        << operands[0];
    for (std::size_t i = 1; i < operands.size(); ++i) {
      out << ", " << operands[i];
    }
    out << "\n";
  }
}
bool Instruction::isTerminator() const {
  return (kind == Kind::RET || kind == Kind::BR || kind == Kind::UNREACHABLE);
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
  }
  for (const auto &param : params) {
    if (param.type.empty()) {
      return false;
    }
  }
  if (isDeclaration) {
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
    printParams(out, params, false);
    out << "\n";
  } else {
    out << "define " << returnType << " @" << name;
    printParams(out, params, true);
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
Function &Module::addFunction(const std::string &name,
                              const std::string &returnType,
                              const std::vector<FunctionParam> &params) {
  Function fn;
  fn.name = name;
  fn.returnType = returnType;
  fn.params = params;
  functions.push_back(std::move(fn));
  return functions.back();
}
void Module::addDeclaration(const std::string &name,
                            const std::string &returnType,
                            const std::vector<FunctionParam> &params) {
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
void Module::addStructType(const std::string &name,
                           const std::vector<std::string> &fields) {
  if (hasType(name)) {
    return;
  }
  types.push_back(NamedType{name, fields});
}
void Module::addVecType() {
  addStructType("%struct.Vec", {"ptr", "i32", "i32"});
}

} // namespace rx::ir