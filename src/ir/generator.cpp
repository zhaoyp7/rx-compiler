#include "ir/generator.h"

#include <iostream>
#include <ostream>

#include "ast/ast.h"
#include "ir/ir.h"
#include "ir/type.h"
#include "sema/analyzer.h"

namespace rx {

IrGenerator::IrGenerator(const Crate &crate, const SemanticAnalyzer &analyzer)
    : crate(crate), analyzer(analyzer) {}

bool IrGenerator::generate(std::ostream &out) {
  ir::Module module;
  ir::TypeMapper types(analyzer);

  for (const auto &item : crate.items) {
    auto *fn = dynamic_cast<const FnDecl *>(item.get());
    if (fn == nullptr || fn->name == "main") {
      continue;
    }
    int symbol = analyzer.fnSymbol(*fn);
    if (symbol == InvalidSymbol) {
      return false;
    }
    TypeId res = analyzer.returnType(symbol);
    const std::vector<TypeId> *params = analyzer.paramTypes(symbol);
    if (params == nullptr) {
      return false;
    }
    if (!types.declare(res, module)) {
      return false;
    }
    std::vector<std::string> paramTypes;
    for (TypeId param : *params) {
      if (!types.declare(param, module)) {
        return false;
      }
      paramTypes.push_back(types.valueType(param));
    }

    module.addDeclaration("_rx_fn_" + fn->name,
                          types.valueType(res),
                          paramTypes);
  }
  module.addSourceMain();
  module.addEntryWrapper();
  if (!module.print(out)) {
    std::cerr << "failed to emit a valid IR module\n";
    return false;
  }
  return true;
}

} // namespace rx
