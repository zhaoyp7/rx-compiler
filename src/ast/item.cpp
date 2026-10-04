#include "visitor.h"

namespace rx {

void FnDecl::accept(AstVisitor &v) { v.visit(*this); }
void StructDecl::accept(AstVisitor &v) { v.visit(*this); }
void ConstDecl::accept(AstVisitor &v) { v.visit(*this); }
void ImplDecl::accept(AstVisitor &v) { v.visit(*this); }

} // namespace rx