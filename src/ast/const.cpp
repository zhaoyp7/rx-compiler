#include "visitor.h"

namespace rx {

void ConstInt::accept(AstVisitor &v) { v.visit(*this); }
void ConstBool::accept(AstVisitor &v) { v.visit(*this); }
void ConstPath::accept(AstVisitor &v) { v.visit(*this); }
void ConstNeg::accept(AstVisitor &v) { v.visit(*this); }

} // namespace rx