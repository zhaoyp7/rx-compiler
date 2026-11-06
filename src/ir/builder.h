#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir.h"
namespace rx::ir {

class IrBuilder {
public:
  explicit IrBuilder(Function &fn);

  std::string getTemp();
  std::string emit(Instruction inst);

  std::string load(const std::string &type, const std::string &ptr);
  void store(const std::string &type, const std::string &value,
             const std::string &ptr);
  std::string binary(const std::string &op, const std::string &type,
                     const std::string &lhs, const std::string &rhs);
  std::string icmp(const std::string &pred, const std::string &type,
                   const std::string &lhs, const std::string &rhs);
  std::string call(const std::string &returnType, const std::string &callee,
                   const std::vector<std::string> &args);
  std::string cast(const std::string &op, const std::string &fromType,
                   const std::string &toType, const std::string &value);
  std::string gep(const std::string &baseType, const std::string &base,
                  const std::vector<std::string> &indices);
  void ret(const std::string &type, const std::string &value);
  void retVoid();

  void startBlock(const std::string &label);
  BasicBlock &getCurrentBlock();
  void branch(const std::string &target);
  void condBranch(const std::string &cond, const std::string &thenLabel,
                  const std::string &elseLabel);
  void unreachable();
  std::string newLabel(const std::string &hint);
  std::string ensureTemp(const std::string &type);
  std::string ensureSlot(int symbol, const std::string &name,
                         const std::string &type);
  std::string getSlot(int symbol) const;

private:
  Function &fn;
  std::size_t now = 0;
  int tempCount = 0;
  int tempSlotCount = 0;
  int entry = 0;
  int blockCount = 0;
  std::unordered_map<int, std::string> slots;
  std::string insertAlloca(const std::string &name, const std::string &type);
};

} // namespace rx::ir