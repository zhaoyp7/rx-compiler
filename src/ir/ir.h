#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace rx::ir {

inline constexpr const char *SourceMain = "_rx_main";
inline constexpr const char *EntryMain = "main";

struct NamedType {
  std::string name;
  std::vector<std::string> fields;
};
struct Instruction {
  enum class Kind { RET, CALL } kind = Kind::RET;
  std::string type;
  std::string callee;
  bool hasRetValue = false;
  int64_t retValue = 0;

  static Instruction makeRetVoid() { return {Kind::RET, "void", "", false, 0}; }
  static Instruction makeRetI32(int64_t v) {
    return {Kind::RET, "i32", "", true, v};
  }
  static Instruction makeCallVoid(const std::string &calleeName) {
    return {Kind::CALL, "void", calleeName, false, 0};
  }
  bool isTerminator() const { return kind == Kind::RET; }
  void print(std::ostream &out) const;
};
struct BasicBlock {
  std::string label;
  std::vector<Instruction> insts;

  bool hasTerminator() const {
    return (!insts.empty() && insts.back().isTerminator());
  }
  bool validate() const;
  void print(std::ostream &out) const;
};
struct Function {
  std::string name;
  std::string returnType;
  std::vector<std::string> paramTypes;
  bool isDeclaration = false;
  std::vector<BasicBlock> blocks;

  bool validate() const;
  bool print(std::ostream &out) const;
};
class Module {
public:
  void addSourceMain();
  void addEntryWrapper();
  Function &addFunction(const std::string &name, const std::string &returnType,
                        const std::vector<std::string> &paramTypes);
  void addDeclaration(const std::string &name, const std::string &returnType,
                      const std::vector<std::string> &paramTypes);
  bool hasType(const std::string &name) const;
  void addStructType(const std::string &name,
                     const std::vector<std::string> &fields);
  void addVecType();
  bool validate() const;
  bool print(std::ostream &out) const;

private:
  std::vector<NamedType> types;
  std::vector<Function> functions;
};

} // namespace rx::ir