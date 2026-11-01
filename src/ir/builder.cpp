#include "ir/builder.h"

namespace rx::ir {
IrBuilder::IrBuilder(Function &fn) : fn(fn) { startBlock("entry"); }
std::string IrBuilder::getTemp() { return "%t" + std::to_string(count++); }
std::string IrBuilder::emit(Instruction inst) {
  BasicBlock &block = fn.blocks[now];
  block.insts.push_back(std::move(inst));
  return block.insts.back().result;
}
void IrBuilder::startBlock(const std::string &label) {
  BasicBlock block;
  block.label = label;
  fn.blocks.push_back(std::move(block));
  now = fn.blocks.size() - 1;
}
BasicBlock &IrBuilder::getCurrentBlock() { return fn.blocks[now]; }
std::string IrBuilder::ensureSlot(int symbol, const std::string &name,
                                  const std::string &type) {
  auto it = slots.find(symbol);
  if (it != slots.end()) {
    return it->second;
  }
  std::string slotName = "%slot." + name + '.' + std::to_string(symbol);
  emit(Instruction{Instruction::Kind::ALLOCA, slotName, type, "", {}});
  slots[symbol] = slotName;
  return slotName;
}
std::string IrBuilder::getSlot(int symbol) const {
  auto it = slots.find(symbol);
  if (it == slots.end()) {
    return "";
  }
  return it->second;
}

} // namespace rx::ir