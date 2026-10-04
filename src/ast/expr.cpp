#include "visitor.h"

namespace rx {

void IntLit::accept(AstVisitor &v) { v.visit(*this); }
void BoolLit::accept(AstVisitor &v) { v.visit(*this); }
void UnitLit::accept(AstVisitor &v) { v.visit(*this); }
void PathExpr::accept(AstVisitor &v) { v.visit(*this); }
void StructLit::accept(AstVisitor &v) { v.visit(*this); }
void ArrayLit::accept(AstVisitor &v) { v.visit(*this); }
void ArrayRepeat::accept(AstVisitor &v) { v.visit(*this); }
void CallExpr::accept(AstVisitor &v) { v.visit(*this); }
void MethodCallExpr::accept(AstVisitor &v) { v.visit(*this); }
void FieldAccessExpr::accept(AstVisitor &v) { v.visit(*this); }
void IndexExpr::accept(AstVisitor &v) { v.visit(*this); }
void UnaryExpr::accept(AstVisitor &v) { v.visit(*this); }
void BinaryExpr::accept(AstVisitor &v) { v.visit(*this); }
void AssignExpr::accept(AstVisitor &v) { v.visit(*this); }
void CastExpr::accept(AstVisitor &v) { v.visit(*this); }
void IfExpr::accept(AstVisitor &v) { v.visit(*this); }
void LoopExpr::accept(AstVisitor &v) { v.visit(*this); }
void WhileExpr::accept(AstVisitor &v) { v.visit(*this); }
void BreakExpr::accept(AstVisitor &v) { v.visit(*this); }
void ContinueExpr::accept(AstVisitor &v) { v.visit(*this); }
void ReturnExpr::accept(AstVisitor &v) { v.visit(*this); }
void BlockExpr::accept(AstVisitor &v) { v.visit(*this); }

} // namespace rx