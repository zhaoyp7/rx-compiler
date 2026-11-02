#include "ir/builder.h"

namespace rx::ir {
IrBuilder::IrBuilder(Function &fn) : fn(fn) { startBlock("entry"); }
std::string IrBuilder::getTemp() { return "%t" + std::to_string(tempCount++); }
std::string IrBuilder::emit(Instruction inst) {
  BasicBlock &block = fn.blocks[now];
  block.insts.push_back(std::move(inst));
  return block.insts.back().result;
}
std::string IrBuilder::load(const std::string &type, const std::string &ptr) {
  std::string result = getTemp();
  emit(Instruction{Instruction::Kind::LOAD, result, type, "", {ptr}});
  return result;
}
void IrBuilder::store(const std::string &type, const std::string &value,
                      const std::string &ptr) {
  emit(Instruction{Instruction::Kind::STORE, "", type, "", {value, ptr}});
}
std::string IrBuilder::binary(const std::string &op, const std::string &type,
                              const std::string &lhs, const std::string &rhs) {
  std::string result = getTemp();
  emit(Instruction{Instruction::Kind::BINARY, result, type, op, {lhs, rhs}});
  return result;
}
std::string IrBuilder::icmp(const std::string &pred, const std::string &type,
                            const std::string &lhs, const std::string &rhs) {
  std::string result = getTemp();
  emit(Instruction{Instruction::Kind::ICMP, result, type, pred, {lhs, rhs}});
  return result;
}
void IrBuilder::ret(const std::string &type, const std::string &value) {
  emit(Instruction{Instruction::Kind::RET, "", type, "", {value}});
}
void IrBuilder::retVoid() { emit(Instruction::makeRetVoid()); }
void IrBuilder::startBlock(const std::string &label) {
  BasicBlock block;
  block.label = label;
  fn.blocks.push_back(std::move(block));
  now = fn.blocks.size() - 1;
}
BasicBlock &IrBuilder::getCurrentBlock() { return fn.blocks[now]; }
void IrBuilder::branch(const std::string &target) {
  emit(Instruction{Instruction::Kind::BR, "", "", "", {target}});
}
void IrBuilder::condBranch(const std::string &cond,
                           const std::string &thenLabel,
                           const std::string &elseLabel) {
  emit(Instruction{
      Instruction::Kind::BR, "", "", "", {cond, thenLabel, elseLabel}});
}
void IrBuilder::unreachable() {
  emit(Instruction{Instruction::Kind::UNREACHABLE, "", "", "", {}});
}
std::string IrBuilder::newLabel(const std::string &hint) {
  return hint + "." + std::to_string(blockCount++);
}
std::string IrBuilder::insertAlloca(const std::string &name,
                                    const std::string &type) {
  BasicBlock &entryBlock = fn.blocks[entry];
  auto pos = entryBlock.insts.end();
  if (!entryBlock.insts.empty() && entryBlock.insts.back().isTerminator()) {
    --pos;
  }
  entryBlock.insts.insert(
      pos, Instruction{Instruction::Kind::ALLOCA, name, type, "", {}});
  return name;
}
std::string IrBuilder::ensureTemp(const std::string &type) {
  std::string slotName = "%tmp." + std::to_string(tempSlotCount++);
  return insertAlloca(slotName, type);
}
std::string IrBuilder::ensureSlot(int symbol, const std::string &name,
                                  const std::string &type) {
  auto it = slots.find(symbol);
  if (it != slots.end()) {
    return it->second;
  }
  std::string slotName = "%slot." + name + '.' + std::to_string(symbol);
  slots[symbol] = slotName;
  return insertAlloca(slotName, type);
}
std::string IrBuilder::getSlot(int symbol) const {
  auto it = slots.find(symbol);
  if (it == slots.end()) {
    return "";
  }
  return it->second;
}
std::string IrBuilder::call(const std::string &returnType,
                            const std::string &callee,
                            const std::vector<std::string> &args) {
  std::string result = returnType == "void" ? "" : getTemp();
  emit(Instruction{Instruction::Kind::CALL, result, returnType, callee, args});
  return result;
}
std::string IrBuilder::cast(const std::string &op, const std::string &fromType,
                            const std::string &toType,
                            const std::string &value) {
  std::string result = getTemp();
  emit(Instruction{
      Instruction::Kind::CAST, result, fromType, op, {value, toType}});
  return result;
}

} // namespace rx::ir