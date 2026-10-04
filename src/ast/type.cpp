#include "visitor.h"

namespace rx {

void UnitType::accept(AstVisitor &v) { v.visit(*this); }
void ParenType::accept(AstVisitor &v) { v.visit(*this); }
void PathType::accept(AstVisitor &v) { v.visit(*this); }
void RefType::accept(AstVisitor &v) { v.visit(*this); }
void ArrayType::accept(AstVisitor &v) { v.visit(*this); }

} // namespace rx