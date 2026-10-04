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

## 7. ASTBuilder ↔ Parser.g4 对照表

对应 `src/ast/builder.h` 的函数划分。状态图例：

- **✅ 独立函数**：builder 里有对应职责的函数
- **🔁 合并**：多条规则进同一个函数的 case，不需要单独写
- **➖ 内联**：父函数直接读 context/token，不单独成函数
- **⛔ 丢弃**：规范明确不处理
- **❌ 纯解析辅助**：只为解析消歧存在，不产生 AST

### 7.1 顶层与 item

| 规则 | 行 | builder | 状态/原因 |
|---|---|---|---|
| `crate` | 7 | `buildCrate` | ✅ |
| `item` | 11 | `buildItem` | ✅ 分发器 |
| `useDeclaration` | 19 | — | ⛔ 规范明确解析后整条丢弃 |
| `useTree` | 23 | — | ⛔ 同上 |
| `usePath` | 28 | — | ⛔ 同上 |
| `usePathSegment` | 32 | — | ⛔ 同上 |
| `functionDefinition` | 36 | `buildFunction` | ✅ |
| `functionParameters` | 41 | `buildParams` | ✅ |
| `selfParam` | 46 | `buildSelfParam` | ✅ |
| `functionParam` | 50 | `buildParam` | ✅ |
| `structDefinition` | 54 | `buildStruct` | ✅ |
| `structField` | 59 | `buildFieldDef` | ✅ |
| `outerAttribute` | 63 | `buildDerives` | ✅ |
| `deriveName` | 68 | — | ➖ `buildDerives` 读文本置位 |
| `constantItem` | 72 | `buildConstItem` | ✅ |
| `inherentImpl` | 76 | `buildImpl` | ✅ |
| `associatedItem` | 80 | — | ➖ `buildImpl` 转发到 `buildFunction`/`buildConstItem` |
| `genericParams` | 84 | — | ⛔ lifetime 语法丢弃 |
| `lifetimeParam` | 88 | — | ⛔ |
| `lifetime` | 92 | — | ⛔ |
| `lifetimeBounds` | 96 | — | ⛔ |
| `typeParamBounds` | 100 | — | ⛔ |
| `whereClause` | 104 | — | ⛔ |
| `whereClauseItem` | 108 | — | ⛔ |

### 7.2 类型与路径

| 规则 | 行 | builder | 状态/原因 |
|---|---|---|---|
| `typeRef` | 113 | `buildType` | ✅ 分发器 |
| `referenceType` | 121 | `buildReferenceType` | ✅（`&&` 拆两层） |
| `arrayType` | 126 | `buildArrayType` | ✅ |
| `typePath` | 130 | `buildPathType` | ✅ |
| `typePathSegment` | 134 | — | ➖ `buildTypePathSegments` 内联 |
| `pathInExpression` | 138 | `buildPathSegments` | ✅ 复用（PathExpr/StructLit/callee） |
| `pathExprSegment` | 142 | — | ➖ `buildPathSegments` 内联 |
| `pathIdentSegment` | 146 | — | ➖ 读文本 + 判 `Self`/`self` |
| `genericArgs` | 150 | `buildGenericArgs` | ✅ |
| `genericArg` | 154 | — | ➖ type 实参递归 `buildType`，lifetime 丢弃 |
| `genericClose` | 158 | — | ❌ 只是 `>` 的拼装 token |
| `closedCastType` | 164 | — | 🔁 合并进 `buildCast` |

### 7.3 常量

| 规则 | 行 | builder | 状态/原因 |
|---|---|---|---|
| `constValue` | 171 | `buildConst` | ✅ |
| `magnitude` | 180 | — | ➖ `buildConst` 处理 `ConstNeg` 操作数 |
| `identifierBinding` | 184 | — | ➖ `buildLet`/`buildParam` 读 name+mut |

### 7.4 语句与块

| 规则 | 行 | builder | 状态/原因 |
|---|---|---|---|
| `letStatement` | 188 | `buildLet` | ✅ |
| `blockExpression` | 192 | `buildBlock` | ✅（含 tail 判断） |
| `statement` | 196 | `buildStmt` | ✅ 分发器（空 `;` 丢弃） |
| `expressionWithBlock` | 203 | — | ➖ `buildPrimary`/`buildStmt` 内产出对应 Expr |
| `ifExpression` | 210 | — | ➖ `buildPrimary` 内处理（可自行加 `buildIf`） |

### 7.5 表达式主链

| 规则 | 行 | builder | 状态/原因 |
|---|---|---|---|
| `expression` | 218 | `buildExpr` | ✅ 统一入口/分发 |
| `assignmentExpression` | 222 | `buildAssignment` | ✅ 右结合 |
| `logicalOrExpression` | 226 | `foldBinary(LOr)` | 🔁 |
| `logicalAndExpression` | 230 | `foldBinary(LAnd)` | 🔁 |
| `comparisonExpression` | 234 | `buildComparison` | ✅ 单运算符 |
| `bitOrExpression` / `closed*` | 239 / 243 | `foldBinary(BitOr)` | 🔁 |
| `bitXorExpression` / `closed*` | 247 / 251 | `foldBinary(BitXor)` | 🔁 |
| `bitAndExpression` / `closed*` | 255 / 259 | `foldBinary(BitAnd)` | 🔁 |
| `shiftExpression` / `closed*` | 263 / 267 | `foldBinary(Shl/Shr)` | 🔁 运算符可能是 `shiftRight` 规则节点 |
| `additiveExpression` / `closed*` | 271 / 275 | `foldBinary(Add/Sub)` | 🔁 |
| `multiplicativeExpression` / `closed*` | 279 / 283 | `foldBinary(Mul/Div/Rem)` | 🔁 |
| `castExpression` / `closedCastExpression` | 287 / 291 | `buildCast` | 🔁 |
| `unaryExpression` | 296 | `buildUnary` | ✅ |
| `postfixExpression` | 301 | `buildPostfix` | ✅ |

### 7.6 影子规则族（全部 🔁 合并，不单独写）

| 族 | 行范围 | 合并进 |
|---|---|---|
| `condition*`（21 条） | 306-393 | `buildAssignment` / `foldBinary` / `buildComparison` / `buildCast` / `buildUnary` / `buildPostfix` / `buildExpr` |
| `conditionBreak*`（19 条） | 395-492 | 同上 |
| `statement*`（20 条） | 494-592 | 同上；`statementPostfixExpression` 的 `expressionWithBlock dotSuffix` 分支在 `buildPostfix` 判定 |

原因：与主链是同一套语法、不同解析入口，`buildExpr` 的 rule-index switch 把它们放进同一个 case。

### 7.7 primary / 后缀 / 运算符 / 叶子

| 规则 | 行 | builder | 状态/原因 |
|---|---|---|---|
| `primaryExpression` | 594 | `buildPrimary` | ✅ |
| `nonBlockPrimary` | 599 | — | ➖ `buildPrimary` 内联 |
| `conditionPrimary` / `conditionPrimaryWithoutBareBlock` | 611 / 618 | — | 🔁 合并进 `buildPrimary` |
| `literalExpression` | 631 | — | ➖ `buildPrimary` 产出 IntLit/BoolLit |
| `structExprFields` / `structExprField` | 635 / 639 | — | ➖ `buildPrimary` 的 StructLit 部分 |
| `arrayExpression` | 643 | — | ➖ `buildPrimary` 的 ArrayLit/ArrayRepeat 部分 |
| `postfixSuffix` | 647 | — | ➖ `buildPostfix` 循环内 |
| `dotSuffix` | 653 | — | ➖ 带参数→MethodCall，否则 FieldAccess |
| `callArguments` | 658 | — | ➖ `buildPostfix`/调用实参 |
| `unaryOperator` | 662 | — | ➖ `buildUnary` 读 token；`ANDAND` 拆两层 |
| `multiplicativeOperator` / `additiveOperator` | 666 / 670 | — | ➖ `foldBinary` 的 op 映射表 |
| `shiftRight` | 676 | — | ➖ `foldBinary` 识别为 `Shr` |
| `comparisonExceptLt` | 680 | — | ➖ `buildComparison` 的 op 映射 |
| `assignmentOperator` / `equalsSign` | 684 / 690 | — | ➖ `buildAssignment` 的 op 映射 |
| `identifier` | 695 | — | ➖ 各构造器直接 `getText()` |

### 7.8 统计

- **独立函数** ≈ 30 个：`buildCrate/Item/Function/Struct/ConstItem/Impl/Params/SelfParam/Param/Derives/FieldDef`、`buildType/ReferenceType/ArrayType/PathType/TypePathSegments`、`buildBlock/Stmt/Let`、`buildExpr/Assignment/foldBinary/Comparison/Cast/Unary/Postfix/Primary`、`buildConst/PathSegments/GenericArgs`；
- **合并（不单独写）**：68 条影子/`closed*` 规则 + `closedCastType`；
- **内联读取（不单独写）**：17 条小规则；
- **丢弃**：`use` 4 条 + lifetime 7 条；
- **纯解析辅助**：`genericClose`。
