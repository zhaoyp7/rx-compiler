#pragma once

#include <iosfwd>

namespace rx {

struct Crate;
class SemanticAnalyzer;

class IrGenerator {
public:
  IrGenerator(const Crate &crate, const SemanticAnalyzer &analyzer);

  bool generate(std::ostream &out);

private:
  const Crate &crate;
  const SemanticAnalyzer &analyzer;
};

} // namespace rx
