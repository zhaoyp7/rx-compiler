#include "dump.h"

#include <sstream>

namespace rx {

namespace {

const char *boolName(bool value) { return value ? "true" : "false"; }

const char *unaryName(UnaryOp op) {
  switch (op) {
  case UnaryOp::Neg:
    return "-";
  case UnaryOp::Not:
    return "!";
  case UnaryOp::Deref:
    return "*";
  case UnaryOp::Ref:
    return "&";
  case UnaryOp::RefMut:
    return "&mut";
  }
  return "?";
}

const char *binaryName(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Sub:
    return "-";
  case BinaryOp::Mul:
    return "*";
  case BinaryOp::Div:
    return "/";
  case BinaryOp::Rem:
    return "%";
  case BinaryOp::BitAnd:
    return "&";
  case BinaryOp::BitOr:
    return "|";
  case BinaryOp::BitXor:
    return "^";
  case BinaryOp::Shl:
    return "<<";
  case BinaryOp::Shr:
    return ">>";
  case BinaryOp::Eq:
    return "==";
  case BinaryOp::Ne:
    return "!=";
  case BinaryOp::Lt:
    return "<";
  case BinaryOp::Le:
    return "<=";
  case BinaryOp::Gt:
    return ">";
  case BinaryOp::Ge:
    return ">=";
  case BinaryOp::LAnd:
    return "&&";
  case BinaryOp::LOr:
    return "||";
  }
  return "?";
}

const char *assignName(AssignOp op) {
  switch (op) {
  case AssignOp::Assign:
    return "=";
  case AssignOp::AddAssign:
    return "+=";
  case AssignOp::SubAssign:
    return "-=";
  case AssignOp::MulAssign:
    return "*=";
  case AssignOp::DivAssign:
    return "/=";
  case AssignOp::RemAssign:
    return "%=";
  case AssignOp::BitAndAssign:
    return "&=";
  case AssignOp::BitOrAssign:
    return "|=";
  case AssignOp::BitXorAssign:
    return "^=";
  case AssignOp::ShlAssign:
    return "<<=";
  case AssignOp::ShrAssign:
    return ">>=";
  }
  return "?";
}

const char *receiverName(ParamKind kind) {
  return kind == ParamKind::Self ? "self" : "value";
}

std::string join(const std::string &sep, const std::vector<std::string> &parts) {
  std::string result;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      result += sep;
    }
    result += parts[i];
  }
  return result;
}

} // namespace

std::string DumpVisitor::suffixName(IntSuffix suffix) {
  switch (suffix) {
  case IntSuffix::None:
    return "";
  case IntSuffix::I32:
    return "i32";
  case IntSuffix::U32:
    return "u32";
  case IntSuffix::Isize:
    return "isize";
  case IntSuffix::Usize:
    return "usize";
  }
  return "";
}

std::string DumpVisitor::pathName(const std::vector<PathSegment> &segments) {
  std::vector<std::string> parts;
  for (const PathSegment &segment : segments) {
    std::string part = segment.name;
    if (segment.kind == PathSegmentKind::SelfType) {
      part = "Self";
    } else if (segment.kind == PathSegmentKind::SelfValue) {
      part = "self";
    }
    if (!segment.genericArgs.empty()) {
      std::vector<std::string> args;
      for (const TypePtr &arg : segment.genericArgs) {
        if (arg) {
          args.push_back(typeName(*arg));
        }
      }
      part += "::<" + join(", ", args) + ">";
    }
    parts.push_back(part);
  }
  return join("::", parts);
}

std::string DumpVisitor::constName(ConstValue &value) {
  if (auto *integer = dynamic_cast<ConstInt *>(&value)) {
    return std::to_string(integer->value) + suffixName(integer->suffix);
  }
  if (auto *boolean = dynamic_cast<ConstBool *>(&value)) {
    return boolName(boolean->value);
  }
  if (auto *path = dynamic_cast<ConstPath *>(&value)) {
    return join("::", path->names);
  }
  if (auto *neg = dynamic_cast<ConstNeg *>(&value)) {
    return neg->operand ? "-" + constName(*neg->operand) : "-?";
  }
  return "?";
}

std::string DumpVisitor::typeName(TypeSyntax &type) {
  if (dynamic_cast<UnitType *>(&type)) {
    return "()";
  }
  if (auto *paren = dynamic_cast<ParenType *>(&type)) {
    return paren->inner ? typeName(*paren->inner) : "()";
  }
  if (auto *path = dynamic_cast<PathType *>(&type)) {
    return pathName(path->segments);
  }
  if (auto *ref = dynamic_cast<RefType *>(&type)) {
    return std::string(ref->mut ? "&mut " : "&") +
           (ref->inner ? typeName(*ref->inner) : "_");
  }
  if (auto *array = dynamic_cast<ArrayType *>(&type)) {
    std::string elem = array->elem ? typeName(*array->elem) : "_";
    std::string len = array->len ? constName(*array->len) : "_";
    return "[" + elem + "; " + len + "]";
  }
  return "?";
}

void DumpVisitor::line(const std::string &text) {
  out_ << std::string(static_cast<std::size_t>(indent_) * 2, ' ') << text
       << "\n";
}

void DumpVisitor::child(Node &node) { node.accept(*this); }

void DumpVisitor::child(Node *node) {
  if (node) {
    node->accept(*this);
  }
}

void DumpVisitor::dump(const Crate &crate) {
  line("Crate");
  ++indent_;
  for (const ItemPtr &item : crate.items) {
    child(*item);
  }
  --indent_;
}

void DumpVisitor::visit(ConstInt &node) {
  line("ConstInt " + std::to_string(node.value) + suffixName(node.suffix));
}

void DumpVisitor::visit(ConstBool &node) {
  line(std::string("ConstBool ") + boolName(node.value));
}

void DumpVisitor::visit(ConstPath &node) {
  line("ConstPath " + join("::", node.names));
}

void DumpVisitor::visit(ConstNeg &node) {
  line("ConstNeg");
  child(node.operand.get());
}

void DumpVisitor::visit(UnitType &) { line("UnitType ()"); }

void DumpVisitor::visit(ParenType &node) {
  line("ParenType");
  child(node.inner.get());
}

void DumpVisitor::visit(PathType &node) {
  line("PathType " + pathName(node.segments));
}

void DumpVisitor::visit(RefType &node) {
  line(std::string("RefType ") + (node.mut ? "&mut" : "&"));
  child(node.inner.get());
}

void DumpVisitor::visit(ArrayType &node) {
  line("ArrayType");
  child(node.elem.get());
  child(node.len.get());
}

void DumpVisitor::visit(IntLit &node) {
  line("IntLit " + std::to_string(node.value) + suffixName(node.suffix));
}

void DumpVisitor::visit(BoolLit &node) {
  line(std::string("BoolLit ") + boolName(node.value));
}

void DumpVisitor::visit(UnitLit &) { line("UnitLit ()"); }

void DumpVisitor::visit(PathExpr &node) {
  line("PathExpr " + pathName(node.segments));
}

void DumpVisitor::visit(StructLit &node) {
  line("StructLit " + pathName(node.path));
  for (FieldInit &field : node.fields) {
    line("FieldInit " + field.name);
    child(field.value.get());
  }
}

void DumpVisitor::visit(ArrayLit &node) {
  line("ArrayLit");
  ++indent_;
  for (ExprPtr &element : node.elements) {
    child(element.get());
  }
  --indent_;
}

void DumpVisitor::visit(ArrayRepeat &node) {
  line("ArrayRepeat");
  ++indent_;
  child(node.elem.get());
  child(node.count.get());
  --indent_;
}

void DumpVisitor::visit(CallExpr &node) {
  line("Call");
  ++indent_;
  child(node.callee.get());
  for (ExprPtr &arg : node.args) {
    child(arg.get());
  }
  --indent_;
}

void DumpVisitor::visit(MethodCallExpr &node) {
  std::string name = "MethodCall " + node.method;
  if (!node.genericArgs.empty()) {
    std::vector<std::string> args;
    for (const TypePtr &arg : node.genericArgs) {
      if (arg) {
        args.push_back(typeName(*arg));
      }
    }
    name += "::<" + join(", ", args) + ">";
  }
  line(name);
  ++indent_;
  child(node.receiver.get());
  for (ExprPtr &arg : node.args) {
    child(arg.get());
  }
  --indent_;
}

void DumpVisitor::visit(FieldAccessExpr &node) {
  line("FieldAccess " + node.field);
  child(node.receiver.get());
}

void DumpVisitor::visit(IndexExpr &node) {
  line("Index");
  ++indent_;
  child(node.base.get());
  child(node.index.get());
  --indent_;
}

void DumpVisitor::visit(UnaryExpr &node) {
  line(std::string("Unary ") + unaryName(node.op));
  child(node.operand.get());
}

void DumpVisitor::visit(BinaryExpr &node) {
  line(std::string("Binary ") + binaryName(node.op));
  ++indent_;
  child(node.lhs.get());
  child(node.rhs.get());
  --indent_;
}

void DumpVisitor::visit(AssignExpr &node) {
  line(std::string("Assign ") + assignName(node.op));
  ++indent_;
  child(node.lhs.get());
  child(node.rhs.get());
  --indent_;
}

void DumpVisitor::visit(CastExpr &node) {
  line("Cast as " + (node.type ? typeName(*node.type) : "?"));
  child(node.expr.get());
}

void DumpVisitor::visit(IfExpr &node) {
  line("If");
  ++indent_;
  child(node.cond.get());
  line("then:");
  ++indent_;
  child(node.then.get());
  --indent_;
  if (node.elseBranch) {
    line("else:");
    ++indent_;
    child(node.elseBranch.get());
    --indent_;
  }
  --indent_;
}

void DumpVisitor::visit(LoopExpr &node) {
  line("Loop");
  child(node.body.get());
}

void DumpVisitor::visit(WhileExpr &node) {
  line("While");
  ++indent_;
  child(node.cond.get());
  child(node.body.get());
  --indent_;
}

void DumpVisitor::visit(BreakExpr &node) {
  line("Break");
  child(node.value.get());
}

void DumpVisitor::visit(ContinueExpr &) { line("Continue"); }

void DumpVisitor::visit(ReturnExpr &node) {
  line("Return");
  child(node.value.get());
}

void DumpVisitor::visit(BlockExpr &node) {
  line("Block");
  ++indent_;
  for (StmtPtr &stmt : node.stmts) {
    child(stmt.get());
  }
  if (node.tail) {
    line("tail:");
    ++indent_;
    child(node.tail.get());
    --indent_;
  }
  --indent_;
}

void DumpVisitor::visit(LetStmt &node) {
  std::string text = "LetStmt ";
  if (node.mut) {
    text += "mut ";
  }
  text += node.name;
  if (node.type) {
    text += ": " + typeName(*node.type);
  }
  line(text);
  child(node.init.get());
}

void DumpVisitor::visit(ExprStmt &node) {
  line("ExprStmt");
  child(node.expr.get());
}

void DumpVisitor::visit(FnDecl &node) {
  line("FnDecl " + node.name);
  ++indent_;
  for (Param &param : node.params) {
    if (param.kind == ParamKind::Self) {
      std::string receiver = "Param self";
      if (param.byMutRef) {
        receiver = "Param &mut self";
      } else if (param.byRef) {
        receiver = "Param &self";
      } else if (param.mutBinding) {
        receiver = "Param mut self";
      }
      line(receiver);
    } else {
      std::string text = "Param " + param.name;
      if (param.mutBinding) {
        text = "Param mut " + param.name;
      }
      text += ": " + (param.type ? typeName(*param.type) : "?");
      line(text);
    }
  }
  line("ret: " + (node.ret ? typeName(*node.ret) : "()"));
  child(node.body.get());
  --indent_;
}

void DumpVisitor::visit(StructDecl &node) {
  std::string derives;
  if (node.derives.copy) {
    derives += "Copy,";
  }
  if (node.derives.clone) {
    derives += "Clone,";
  }
  if (node.derives.partialEq) {
    derives += "PartialEq,";
  }
  if (node.derives.eq) {
    derives += "Eq,";
  }
  if (!derives.empty()) {
    derives.pop_back();
  }
  line("StructDecl " + node.name + (derives.empty() ? "" : " derive(" + derives + ")"));
  ++indent_;
  for (FieldDef &field : node.fields) {
    line("FieldDef " + field.name + ": " +
         (field.type ? typeName(*field.type) : "?"));
  }
  --indent_;
}

void DumpVisitor::visit(ConstDecl &node) {
  line("ConstDecl " + node.name + ": " +
       (node.type ? typeName(*node.type) : "?"));
  child(node.value.get());
}

void DumpVisitor::visit(ImplDecl &node) {
  line("ImplDecl " + (node.target ? typeName(*node.target) : "?"));
  ++indent_;
  for (ItemPtr &member : node.members) {
    child(member.get());
  }
  --indent_;
}

} // namespace rx
