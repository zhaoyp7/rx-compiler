#include "builder.h"

namespace rx {

BuildResult ASTBuilder::build(RxParser::CrateContext *tree) {
  BuildResult result;
  result.crate = buildCrate(tree);
  result.ok = ok_;
  return result;
}

// TODO(P2): 从这里开始实现，其余函数在下方补齐。
Crate ASTBuilder::buildCrate(RxParser::CrateContext *ctx) {
  (void)ctx;
  fail();
  return Crate{};
}

void ASTBuilder::fail() { ok_ = false; }

} // namespace rx
