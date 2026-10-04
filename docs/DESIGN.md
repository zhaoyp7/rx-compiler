# Rx 编译器设计文档

> 阶段一（AST + 语义）设计记录。本文件随阶段推进更新。
> 当前进度：P1（AST 定义）完成，P2（AST 构建）未开始。

## 1. AST 总体设计

### 继承结构

```
Node (SourceSpan span; virtual ~Node; virtual void accept(AstVisitor&))
├── ConstValue ── ConstInt / ConstBool / ConstPath / ConstNeg
├── TypeSyntax ── UnitType / ParenType / PathType / RefType / ArrayType
├── Expr ──────── IntLit / BoolLit / UnitLit / PathExpr / StructLit /
│                ArrayLit / ArrayRepeat / CallExpr / MethodCallExpr /
│                FieldAccessExpr / IndexExpr / UnaryExpr / BinaryExpr /
│                AssignExpr / CastExpr / IfExpr / LoopExpr / WhileExpr /
│                BreakExpr / ContinueExpr / ReturnExpr / BlockExpr
├── Stmt ──────── LetStmt / ExprStmt
└── Item ──────── FnDecl / StructDecl / ConstDecl / ImplDecl

Crate { items: vector<ItemPtr> }   // AST 根，不继承 Node
```

### 文件组织

| 文件 | 内容 |
|---|---|
| `src/ast/ast.h` | SourceSpan、Node 基类、指针别名、枚举、公共小结构、全部节点 |
| `src/ast/visitor.h` | `AstVisitor`，每个具体节点一个纯虚 `visit` |
| `src/ast/const.cpp` / `type.cpp` / `expr.cpp` / `stmt.cpp` / `item.cpp` | 各节点 `accept` 实现（一行转发） |

### 遍历机制

`accept(AstVisitor&)` 双分派。后续每个 pass（dump、符号收集、类型检查、IR）实现一个 `AstVisitor` 子类；新增节点会强制所有 pass 处理（纯虚编译报错）。

### 内存模型

- 树的所有权：父节点用 `std::unique_ptr` 持有子节点，列表用 `std::vector<unique_ptr<T>>`；
- 可选单子节点用 `unique_ptr` + `nullptr`；
- 父节点析构自动递归释放整棵树，无手工 `delete`；
- AST 节点是 move-only，禁止拷贝。

## 2. 设计约定

| 约定 | 说明 |
|---|---|
| `SourceSpan` | 每个 `Node` 都有；记录字节偏移与起始行列 |
| `FnDecl::ret == nullptr` | 表示省略了 `-> T`，语义为 `()`；显式 `-> ()` 是 `UnitType` |
| `Param` 的合法组合 | 见下表 |
| 双重引用 | `&&x` 解析为两层 `UnaryExpr{Ref}`；`&&mut x` 为 `Ref(RefMut(x))` |
| `RefType` 同理 | `&&i32` 为两层 `RefType` |
| 块尾表达式 | 块中最后一条不带分号的表达式（含 `if/loop/while/block`）进入 `BlockExpr::tail`；其余表达式语句进 `stmts` |
| 空语句 | 单独的 `;` 直接丢弃，不产生节点 |
| `use` 声明 | 整条丢弃，不建节点 |
| lifetime 语法 | 泛型参数、引用标注、实参、bound、where 全部丢弃 |
| 语义信息 | P1 不挂类型/符号/place；P5/P6 再考虑字段或旁表 |
| `Crate` | 根节点，不继承 `Node`、没有 span |

`Param` 合法组合：

| 源码 | kind | byRef | byMutRef | mutBinding |
|---|---|---|---|---|
| `x: T` | Value | false | false | false |
| `mut x: T` | Value | false | false | true |
| `self` | Self | false | false | false |
| `mut self` | Self | false | false | true |
| `&self` | Self | true | false | false |
| `&mut self` | Self | true | true | false |

## 3. 文法覆盖勾选表

说明：行号对应 `grammar/Parser.g4`。三类结果——**建节点**、**归一化到已有节点**、**明确丢弃**。

### 3.1 顶层与 item

| 产生式 | 行 | AST 结果 | 状态 |
|---|---|---|---|
| `crate` | 7 | `Crate{items}` | [x] |
| `item` | 11 | FnDecl / StructDecl / ConstDecl / ImplDecl | [x] |
| `useDeclaration` | 19 | 丢弃 | [x] |
| `useTree` | 23 | 丢弃 | [x] |
| `usePath` | 28 | 丢弃 | [x] |
| `usePathSegment` | 32 | 丢弃 | [x] |
| `functionDefinition` | 36 | `FnDecl{name, params, ret, body}` | [x] |
| `functionParameters` | 41 | `vector<Param>` | [x] |
| `selfParam` | 46 | `Param{kind=Self, byRef, byMutRef, mutBinding}` | [x] |
| `functionParam` | 50 | `Param{kind=Value, name, type}` | [x] |
| `structDefinition` | 54 | `StructDecl{name, derives, fields}` | [x] |
| `structField` | 59 | `FieldDef{name, type}` | [x] |
| `outerAttribute` | 63 | `Derives` | [x] |
| `deriveName` | 68 | `Derives` 对应位 | [x] |
| `constantItem` | 72 | `ConstDecl{name, type, value}` | [x] |
| `inherentImpl` | 76 | `ImplDecl{target, members}` | [x] |
| `associatedItem` | 80 | member 的 FnDecl / ConstDecl | [x] |
| `genericParams` | 84 | 丢弃（lifetime） | [x] |
| `lifetimeParam` | 88 | 丢弃 | [x] |
| `lifetime` | 92 | 丢弃 | [x] |
| `lifetimeBounds` | 96 | 丢弃 | [x] |
| `typeParamBounds` | 100 | 丢弃 | [x] |
| `whereClause` | 104 | 丢弃 | [x] |
| `whereClauseItem` | 108 | 丢弃 | [x] |

### 3.2 类型与路径

| 产生式 | 行 | AST 结果 | 状态 |
|---|---|---|---|
| `typeRef` | 113 | TypeSyntax（递归） | [x] |
| `referenceType` | 121 | `RefType{mut, inner}`；`ANDAND` 拆两层 | [x] |
| `arrayType` | 126 | `ArrayType{elem, len}` | [x] |
| `typePath` | 130 | `PathType{segments}` | [x] |
| `typePathSegment` | 134 | `PathSegment{name, genericArgs}` | [x] |
| `pathInExpression` | 138 | `PathExpr{segments}`（也可作为 StructLit / callee） | [x] |
| `pathExprSegment` | 142 | `PathSegment` | [x] |
| `pathIdentSegment` | 146 | `PathSegment{name, kind}`（Ident/SelfValue/SelfType） | [x] |
| `genericArgs` | 150 | `PathSegment::genericArgs`（type 实参保留） | [x] |
| `genericArg` | 154 | TypePtr；lifetime 实参丢弃 | [x] |
| `genericClose` | 158 | 仅解析消歧，无节点 | [x] |
| `closedCastType` | 164 | 仅解析消歧，结果落到 `CastExpr::type` | [x] |

### 3.3 常量

| 产生式 | 行 | AST 结果 | 状态 |
|---|---|---|---|
| `constValue` | 171 | ConstInt / ConstBool / ConstPath / ConstNeg | [x] |
| `magnitude` | 180 | ConstInt / ConstPath（`ConstNeg` 的操作数） | [x] |

### 3.4 语句与块

| 产生式 | 行 | AST 结果 | 状态 |
|---|---|---|---|
| `identifierBinding` | 184 | LetStmt / Param 的 `name` + `mut` | [x] |
| `letStatement` | 188 | `LetStmt{mut, name, type, init}` | [x] |
| `blockExpression` | 192 | `BlockExpr{stmts, tail}` | [x] |
| `statement` | 196 | LetStmt / ExprStmt；空 `;` 丢弃 | [x] |
| `expressionWithBlock` | 203 | BlockExpr / IfExpr / LoopExpr / WhileExpr | [x] |
| `ifExpression` | 210 | `IfExpr{cond, then, elseBranch}` | [x] |

### 3.5 表达式主链（普通优先级）

| 产生式 | 行 | AST 结果 | 状态 |
|---|---|---|---|
| `expression` | 218 | Expr | [x] |
| `assignmentExpression` | 222 | `AssignExpr{op, lhs, rhs}` | [x] |
| `logicalOrExpression` | 226 | `BinaryExpr{LOr}` | [x] |
| `logicalAndExpression` | 230 | `BinaryExpr{LAnd}` | [x] |
| `comparisonExpression` | 234 | `BinaryExpr{Eq/Ne/Lt/Le/Gt/Ge}` | [x] |
| `bitOrExpression` / `bitXorExpression` / `bitAndExpression` | 239 / 247 / 255 | `BinaryExpr{BitOr/BitXor/BitAnd}` | [x] |
| `shiftExpression` | 263 | `BinaryExpr{Shl/Shr}` | [x] |
| `additiveExpression` | 271 | `BinaryExpr{Add/Sub}` | [x] |
| `multiplicativeExpression` | 279 | `BinaryExpr{Mul/Div/Rem}` | [x] |
| `castExpression` | 287 | `CastExpr`（链式可嵌套） | [x] |
| `unaryExpression` | 296 | `UnaryExpr{op, operand}` | [x] |
| `postfixExpression` | 301 | 链式 Call / Index / Field / MethodCall | [x] |
| `primaryExpression` / `nonBlockPrimary` | 594 / 599 | 字面量 / 路径 / StructLit / 数组 / break / return / continue / 块类 | [x] |

### 3.6 影子规则族（归一化，不建新节点）

| 产生式族 | 行范围 | 归一化为 | 状态 |
|---|---|---|---|
| `closedBitOrExpression` … `closedCastExpression`（8 条） | 243-291 | 对应普通规则节点 | [x] |
| `conditionExpression` 及 `condition*`（21 条） | 306-393 | 对应普通表达式节点 | [x] |
| `conditionBreak*`（19 条） | 395-492 | 对应普通表达式节点 | [x] |
| `statementExpression` 及 `statement*`（20 条） | 494-592 | 对应普通表达式节点；`statementPostfixExpression` 允许 `expressionWithBlock` 后接 `dotSuffix` | [x] |
| `conditionPrimary` / `conditionPrimaryWithoutBareBlock` | 611 / 618 | 与 `primaryExpression` 相同 | [x] |

### 3.7 叶子与工具规则

| 产生式 | 行 | AST 结果 | 状态 |
|---|---|---|---|
| `literalExpression` | 631 | `IntLit` / `BoolLit` | [x] |
| `structExprFields` / `structExprField` | 635 / 639 | `vector<FieldInit>` | [x] |
| `arrayExpression` | 643 | `ArrayLit` / `ArrayRepeat` | [x] |
| `postfixSuffix` | 647 | Call / Index / dot 链 | [x] |
| `dotSuffix` | 653 | `MethodCallExpr` / `FieldAccessExpr` | [x] |
| `callArguments` | 658 | `args` | [x] |
| `unaryOperator` | 662 | `UnaryOp` | [x] |
| `multiplicativeOperator` / `additiveOperator` | 666 / 670 | `BinaryOp` | [x] |
| `shiftRight` | 676 | `BinaryOp::Shr` | [x] |
| `comparisonExceptLt` | 680 | `BinaryOp` 比较族 | [x] |
| `assignmentOperator` | 684 | `AssignOp` | [x] |
| `equalsSign` | 690 | `AssignOp::Assign`（辅助） | [x] |
| `identifier` | 695 | 名字字符串（含上下文关键字） | [x] |

### 3.8 明确丢弃清单

- `use` 全族（19-32）：import 解析不做。
- lifetime 全族（84-112）：参数、标注、实参、bound、where 全部丢弃。
- 空 `;` 语句、括号表达式外壳、各类影子规则与 `closed*` 辅助规则。

## 4. 示例 AST

源码（`tests/official/semantic/entry/acc-explicit-unit-result-and-early-completion.rx` 的简化版）：

```rust
fn add(a: i32, b: i32) -> i32 {
    a + b
}
```

对应 AST：

```
Crate
└── FnDecl name="add"
    ├── Param{kind=Value, name="a", type=PathType{"i32"}}
    ├── Param{kind=Value, name="b", type=PathType{"i32"}}
    ├── ret = PathType{"i32"}
    └── body = BlockExpr
        ├── stmts = []
        └── tail = BinaryExpr{op=Add}
            ├── lhs = PathExpr{segments=[{name="a", kind=Ident}]}
            └── rhs = PathExpr{segments=[{name="b", kind=Ident}]}
```

## 5. P1 验收记录

| 检查 | 结果 |
|---|---|
| `cmake --build build -j32` | 通过 |
| 具体节点数 / `accept` 定义数 | 37 / 37 |
| `nm` 未定义 `accept` 引用 | 0 |
| `AstVisitor` 纯虚 visit 数 | 37 |
| 236 个 semantic 源码解析 | 0 失败 |
| 文法覆盖勾选 | 本文件第 3 节，全部 [x] |

## 6. P2 实现提醒

- 块尾：最后一条不带分号的表达式（含裸 `if/loop/while/block`）进 `tail`，规范依据 `rx-spec-zh/11-expressions/11.03-block-expr.md`。
- `&&` / `&&mut`：按文法约定展开为嵌套引用。
- `PathExpr` 与 `StructLit`：路径后跟 `{...}` 视为 `StructLit`。
- 结构体字面量在条件位置有额外限制，文法已消歧，直接接结果。
- 每个自定义测试尽量覆盖本表一行，P2 完成后再更新本文件的验收记录。
