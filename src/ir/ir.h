#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace rx::ir {

inline constexpr const char *SourceMain = "_rx_main";
inline constexpr const char *EntryMain = "main";

struct FunctionParam {
  std::string type;
  std::string name;
};
struct LowerResult {
  bool ok = false;
  bool terminated = false;
  bool hasValue = false;
  std::string value;
};
struct NamedType {
  std::string name;
  std::vector<std::string> fields;
};
struct Instruction {
  enum class Kind{RET, CALL, ALLOCA, LOAD, STORE, BINARY, ICMP, CAST, BR, UNREACHABLE, GEP};
  Kind kind = Kind::RET;
  std::string result;
  std::string type;
  std::string op;
  std::vector <std::string> operands;

  static Instruction makeRetVoid() {
    return {Kind::RET, "", "void", "", {}};
  }
  static Instruction makeRetI32(int64_t v) {
    return {Kind::RET, "", "i32", "", {std::to_string(v)}};
  }
  static Instruction makeCallVoid(const std::string &callee) {
    return {Kind::CALL, "", "void", callee, {}};
  }
  bool isTerminator() const;
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
  std::vector<FunctionParam> params;
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
                        const std::vector<FunctionParam> &paramTypes);
  void addDeclaration(const std::string &name, const std::string &returnType,
                      const std::vector<FunctionParam> &paramTypes);
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