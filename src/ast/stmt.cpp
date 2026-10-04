#include "visitor.h"

namespace rx {

void LetStmt::accept(AstVisitor &v) { v.visit(*this); }
void ExprStmt::accept(AstVisitor &v) { v.visit(*this); }

} // namespace rx