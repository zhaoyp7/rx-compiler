#include "ir/generator.h"

#include <ostream>

#include "ast/ast.h"
#include "sema/analyzer.h"

namespace rx {

IrGenerator::IrGenerator(const Crate &crate, const SemanticAnalyzer &analyzer)
    : crate(crate), analyzer(analyzer) {}

bool IrGenerator::generate(std::ostream &out) {
  out << "target triple = \"riscv32-unknown-elf\"\n";
  out << "target datalayout = \"e-m:e-p:32:32-i64:64-n32-S128\"\n\n";

  // TODO(I1): lower the source functions in `crate` using `analyzer`.
  out << "define i32 @main() {\n";
  out << "entry:\n";
  out << "  call void @rx_main()\n";
  out << "  ret i32 0\n";
  out << "}\n\n";

  out << "define void @rx_main() {\n";
  out << "entry:\n";
  out << "  ret void\n";
  out << "}\n";
  return static_cast<bool>(out);
}

} // namespace rx
