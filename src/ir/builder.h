#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "ir.h"
namespace rx::ir {

class IrBuilder{
public:
  explicit IrBuilder(Function &fn);

  std::string getTemp();
  std::string emit(Instruction inst);
  void startBlock(const std::string &label);
  BasicBlock &getCurrentBlock();
  std::string ensureSlot(int symbol, const std::string &name, const std::string &type);
  std::string getSlot(int symbol) const;

private:
  Function &fn;
  std::size_t now = 0;
  int count = 0;
  std::unordered_map<int, std::string> slots;
};

} // namespace rx::ir