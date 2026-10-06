# Rx 编译器设计文档

> 阶段一（AST + 语义）设计记录。本文件随阶段推进更新。
> 当前进度：P1（AST 定义）、P2（AST 构建）完成；下一步 P3（符号与名字解析）。

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
| `src/ast/builder.h` / `builder.cpp` | `ASTBuilder`：parse tree → Crate |
| `src/ast/dump.h` / `dump.cpp` | `DumpVisitor`：AST → 缩进文本 |

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

## 3. 规则映射总表

每一行同时回答：这条文法规则**产出什么节点**、**用哪个 builder 函数**。行号对应 `grammar/Parser.g4`。

状态图例：

- **✅ 独立函数**：builder 里有对应职责的函数
- **🔀 分发器**：按备选判空/分派，返回子 build 结果，自己不建节点
- **🔁 合并**：多套规则进同一个函数的 case
- **➖ 内联**：父函数直接读 context/token，不单独成函数
- **⛔ 丢弃**：规范明确不处理
- **❌ 纯解析辅助**：只为解析消歧存在，不产生 AST

### 3.1 顶层与 item

| 规则 | 行 | 目标节点 | builder | 状态/备注 |
|---|---|---|---|---|
| `crate` | 7 | `Crate{items}` | `buildCrate` | ✅ |
| `item` | 11 | FnDecl / StructDecl / ConstDecl / ImplDecl | `buildItem` | 🔀 分发器 |
| `useDeclaration` | 19 | — | — | ⛔ 规范：解析后整条丢弃 |
| `useTree` | 23 | — | — | ⛔ |
| `usePath` | 28 | — | — | ⛔ |
| `usePathSegment` | 32 | — | — | ⛔ |
| `functionDefinition` | 36 | `FnDecl{name, params, ret, body}` | `buildFunction` | ✅ |
| `functionParameters` | 41 | `vector<Param>` | `buildParams` | ✅ |
| `selfParam` | 46 | `Param{kind=Self}` | `buildSelfParam` | ✅ 只读 token |
| `functionParam` | 50 | `Param{kind=Value}` | `buildParam` | ✅ |
| `structDefinition` | 54 | `StructDecl{name, derives, fields}` | `buildStruct` | ✅ |
| `structField` | 59 | `FieldDef{name, type}` | `buildFieldDef` | ✅ |
| `outerAttribute` | 63 | `Derives` | `buildDerives` | ✅ |
| `deriveName` | 68 | `Derives` 对应位 | — | ➖ `buildDerives` 读文本置位 |
| `constantItem` | 72 | `ConstDecl{name, type, value}` | `buildConstItem` | ✅ |
| `inherentImpl` | 76 | `ImplDecl{target, members}` | `buildImpl` | ✅ |
| `associatedItem` | 80 | member 的 FnDecl / ConstDecl | — | ➖ `buildImpl` 转发 |
| `genericParams` | 84 | — | — | ⛔ lifetime 语法丢弃 |
| `lifetimeParam` | 88 | — | — | ⛔ |
| `lifetime` | 92 | — | — | ⛔ |
| `lifetimeBounds` | 96 | — | — | ⛔ |
| `typeParamBounds` | 100 | — | — | ⛔ |
| `whereClause` | 104 | — | — | ⛔ |
| `whereClauseItem` | 108 | — | — | ⛔ |

### 3.2 类型与路径

| 规则 | 行 | 目标节点 | builder | 状态/备注 |
|---|---|---|---|---|
| `typeRef` | 113 | UnitType / ParenType / PathType / RefType / ArrayType | `buildType` | 🔀 分发器 |
| `referenceType` | 121 | `RefType{mut, inner}` | `buildReferenceType` | ✅ `&&` 拆两层 |
| `arrayType` | 126 | `ArrayType{elem, len}` | `buildArrayType` | ✅ len 调 `buildConst` |
| `typePath` | 130 | `PathType{segments}` | `buildPathType` | ✅ |
| `typePathSegment` | 134 | `PathSegment{name, genericArgs}` | — | ➖ `buildTypePathSegments` |
| `pathInExpression` | 138 | `PathExpr{segments}` | `buildPathSegments` | ✅ 复用（StructLit/callee） |
| `pathExprSegment` | 142 | `PathSegment` | — | ➖ |
| `pathIdentSegment` | 146 | `PathSegment{name, kind}` | — | ➖ 读文本 + 判 Self/self |
| `genericArgs` | 150 | `vector<TypePtr>` | `buildGenericArgs` | ✅ |
| `genericArg` | 154 | `TypePtr`（lifetime 丢弃） | — | ➖ type 实参递归 `buildType` |
| `genericClose` | 158 | — | — | ❌ 只是 `>` 的拼装 token |
| `closedCastType` | 164 | TypeSyntax（closed cast 形态） | — | 🔁 合并进 `buildType`/`buildCast` |

### 3.3 常量

| 规则 | 行 | 目标节点 | builder | 状态/备注 |
|---|---|---|---|---|
| `constValue` | 171 | ConstInt / ConstBool / ConstPath / ConstNeg | `buildConst` | ✅ |
| `magnitude` | 180 | ConstInt / ConstPath | — | ➖ `buildConst` 处理 `ConstNeg` 操作数 |
| `identifierBinding` | 184 | LetStmt / Param 的 name+mut | — | ➖ `buildLet`/`buildParam` 读取 |

### 3.4 语句与块

| 规则 | 行 | 目标节点 | builder | 状态/备注 |
|---|---|---|---|---|
| `letStatement` | 188 | `LetStmt{mut, name, type, init}` | `buildLet` | ✅ |
| `blockExpression` | 192 | `BlockExpr{stmts, tail}` | `buildBlock` | ✅ 含 tail 判断 |
| `statement` | 196 | LetStmt / ExprStmt；空 `;` 丢弃 | `buildStmt` | 🔀 分发器 |
| `expressionWithBlock` | 203 | BlockExpr / IfExpr / LoopExpr / WhileExpr | — | ➖ `buildPrimary`/`buildStmt` 内 |
| `ifExpression` | 210 | `IfExpr{cond, then, elseBranch}` | — | ➖ `buildPrimary` 内 |

### 3.5 表达式主链

| 规则 | 行 | 目标节点 | builder | 状态/备注 |
|---|---|---|---|---|
| `expression` | 218 | Expr | `buildExpr` | 🔀 统一入口/分发 |
| `assignmentExpression` | 222 | `AssignExpr{op, lhs, rhs}` | `buildAssignment` | ✅ 右结合 |
| `logicalOrExpression` | 226 | `BinaryExpr{LOr}` | `foldBinary` | 🔁 |
| `logicalAndExpression` | 230 | `BinaryExpr{LAnd}` | `foldBinary` | 🔁 |
| `comparisonExpression` | 234 | `BinaryExpr{Eq/Ne/Lt/Le/Gt/Ge}` | `buildComparison` | ✅ 单运算符 |
| `bitOrExpression` / `closedBitOrExpression` | 239 / 243 | `BinaryExpr{BitOr}` | `foldBinary` | 🔁 |
| `bitXorExpression` / `closedBitXorExpression` | 247 / 251 | `BinaryExpr{BitXor}` | `foldBinary` | 🔁 |
| `bitAndExpression` / `closedBitAndExpression` | 255 / 259 | `BinaryExpr{BitAnd}` | `foldBinary` | 🔁 |
| `shiftExpression` / `closedShiftExpression` | 263 / 267 | `BinaryExpr{Shl/Shr}` | `foldBinary` | 🔁 `shiftRight` 可能是规则节点 |
| `additiveExpression` / `closedAdditiveExpression` | 271 / 275 | `BinaryExpr{Add/Sub}` | `foldBinary` | 🔁 |
| `multiplicativeExpression` / `closedMultiplicativeExpression` | 279 / 283 | `BinaryExpr{Mul/Div/Rem}` | `foldBinary` | 🔁 |
| `castExpression` / `closedCastExpression` | 287 / 291 | `CastExpr{expr, type}` | `buildCast` | 🔁 可链式 |
| `unaryExpression` | 296 | `UnaryExpr{op, operand}` | `buildUnary` | ✅ |
| `postfixExpression` | 301 | Call / Index / Field / MethodCall 链 | `buildPostfix` | ✅ |

### 3.6 影子规则族（🔁 合并，不单独写）

| 规则族 | 行范围 | 目标节点 | 合并进 |
|---|---|---|---|
| `conditionExpression` 及 `condition*`（21 条） | 306-393 | 对应普通表达式节点 | `buildExpr` 分发 + 同级函数 |
| `conditionBreak*`（19 条） | 395-492 | 同上 | 同上 |
| `statementExpression` 及 `statement*`（20 条） | 494-592 | 同上；`statementPostfixExpression` 的 `expressionWithBlock dotSuffix` 分支特判 | `buildPostfix` 判定 |
| `conditionPrimary` / `conditionPrimaryWithoutBareBlock` | 611 / 618 | 同 `primaryExpression` | `buildPrimary` |

原因：与主链是同一套语法、不同解析入口，`buildExpr` 的 rule-index switch 把它们放进同一个 case。

### 3.7 primary / 后缀 / 运算符 / 叶子

| 规则 | 行 | 目标节点 | builder | 状态/备注 |
|---|---|---|---|---|
| `primaryExpression` | 594 | 字面量 / 路径 / StructLit / 数组 / 控制流 | `buildPrimary` | ✅ |
| `nonBlockPrimary` | 599 | 同上 | — | ➖ `buildPrimary` 内联 |
| `literalExpression` | 631 | `IntLit` / `BoolLit` | — | ➖ `buildPrimary` 内 |
| `structExprFields` / `structExprField` | 635 / 639 | `vector<FieldInit>` | — | ➖ `buildPrimary` 的 StructLit 部分 |
| `arrayExpression` | 643 | `ArrayLit` / `ArrayRepeat` | — | ➖ `buildPrimary` 的数组部分 |
| `postfixSuffix` | 647 | Call / Index / dot 链 | — | ➖ `buildPostfix` 循环内 |
| `dotSuffix` | 653 | `MethodCallExpr` / `FieldAccessExpr` | — | ➖ 带参数→MethodCall，否则 FieldAccess |
| `callArguments` | 658 | `args` | — | ➖ `buildPostfix`/调用实参 |
| `unaryOperator` | 662 | `UnaryOp` | — | ➖ `buildUnary` 读 token；`ANDAND` 拆两层 |
| `multiplicativeOperator` / `additiveOperator` | 666 / 670 | `BinaryOp` | — | ➖ `foldBinary` 的 op 映射表 |
| `shiftRight` | 676 | `BinaryOp::Shr` | — | ➖ `foldBinary` 识别 |
| `comparisonExceptLt` | 680 | `BinaryOp` 比较族 | — | ➖ `buildComparison` 的 op 映射 |
| `assignmentOperator` / `equalsSign` | 684 / 690 | `AssignOp` | — | ➖ `buildAssignment` 的 op 映射 |
| `identifier` | 695 | 名字字符串 | — | ➖ 各构造器直接 `getText()` |

### 3.8 汇总

- **独立函数** ≈ 30 个：`buildCrate/Item/Function/Struct/ConstItem/Impl/Params/SelfParam/Param/Derives/FieldDef`、`buildType/ReferenceType/ArrayType/PathType/TypePathSegments`、`buildBlock/Stmt/Let`、`buildExpr/Assignment/foldBinary/Comparison/Cast/Unary/Postfix/Primary`、`buildConst/PathSegments/GenericArgs`；
- **合并（不单独写）**：68 条影子/`closed*` 规则 + `closedCastType`；
- **内联读取（不单独写）**：17 条小规则；
- **丢弃**：`use` 4 条 + lifetime 7 条 + 空 `;`、括号壳；
- **纯解析辅助**：`genericClose`。

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

## 5. 验收记录

### 5.1 P1（AST 定义）

| 检查 | 结果 |
|---|---|
| `cmake --build build -j32` | 通过 |
| 具体节点数 / `accept` 定义数 | 37 / 37 |
| `nm` 未定义 `accept` 引用 | 0 |
| `AstVisitor` 纯虚 visit 数 | 37 |
| 236 个 semantic 源码解析 | 0 失败 |
| 文法覆盖勾选 | 本文件第 3 节总表，全部 [x] |

### 5.2 P2（AST 构建）

| 检查 | 结果 |
|---|---|
| `cmake --build build -j32` | 通过，0 warning / 0 error |
| `entry` 用例 `--stage parse` / `--stage ast` | 均 exit 0，dump 结构正确 |
| 236 个 semantic 源码 `--stage ast` 全量烟测 | 236/236 通过，0 失败 |
| 抽检 5 个文件人工核对 | `integer-arithmetic`、`arrays`、`loops-and-jumps`、`structs-and-fields`、`comprehensive-quicksort`，结构均正确 |
| 覆盖范围 | 89 条表达式规则经 `buildExpr` 分发，37 个 builder 函数与第 3 节总表一致 |
| 提交 | `4a6fa8f feat(ast): implement complete AST builder` |

抽检确认过的关键归一化：块尾表达式（含裸 `if/loop/while` 与 `while { ... }` 的块条件）、二元左结合、`&&`/`&&mut` 拆两层、`ArrayRepeat` 与 `ArrayLit` 区分、`StructLit`/`FieldInit`、`&mut Vec::<i32>` 路径泛型实参。

## 6. P2 实现提醒

- 块尾：最后一条不带分号的表达式（含裸 `if/loop/while/block`）进 `tail`，规范依据 `rx-spec-zh/11-expressions/11.03-block-expr.md`。
- `&&` / `&&mut`：按文法约定展开为嵌套引用。
- `PathExpr` 与 `StructLit`：路径后跟 `{...}` 视为 `StructLit`。
- 结构体字面量在条件位置有额外限制，文法已消歧，直接接结果。
- 每个自定义测试尽量覆盖本表一行，P2 完成后再更新本文件的验收记录。

## 7. ASTBuilder 函数形态速查

### 7.1 五种函数形状

| 文法形状 | 函数形状 | 例子 |
|---|---|---|
| 多备选（`A \| B \| C`） | 分发器：if / dynamic_cast 判分支，返回子 build 结果，自己不建节点 | `buildItem`、`buildStmt`、`buildType` |
| 单备选 + 若干子节点 | 构造器：`make_unique<Xxx>`，逐字段填 | `buildFunction`、`buildBlock`、`buildStruct` |
| 纯 token 规则 | 直接填一个小结构，不递归 | `buildSelfParam`、`buildDerives` |
| 单子节点透传 | `return buildXxx(child)` | `expression → assignmentExpression`；`(T) → T` |
| 重复子节点（`*`/`+`） | 循环 + `push_back` | `buildCrate` 的 `item()*`、`buildParams` |

### 7.2 写之前的固定三步

1. **查目标**：在本文件第 3 节总表找到「目标节点」和「builder」，确定返回类型；
2. **判断“或/且”**：
   - 规则是 `A | B | C` → 分发，不建节点；
   - 规则是 `A B C` → 构造，把子节点映射到字段；
   - 子节点是 token → 就地读（`getText()`、`MUT() != nullptr`）；
   - 子节点是规则且 AST 类型等于字段类型 → 调它的 build；
   - 子节点没有 AST 价值（lifetime、括号壳）→ 丢弃或内提；
3. **处理可空与重复**：`?` 判 nullptr（能缺省留空，缺省不了 `fail()`）；`*`/`+` 循环；核对丢弃清单。

### 7.3 自检清单（每个函数写完问 5 个问题）

1. 目标节点的每个字段都填了吗（或有意留空）？
2. 每条有语义的文法子节点是否恰好被消费一次？
3. `?` 子节点都有判空吗？缺省语义对吗（如 `ret=nullptr` 表示 `()`）？
4. `*` 子节点都进 vector 了吗？顺序对吗？
5. 丢弃清单核对过了吗（`use`、lifetime、空 `;`、括号壳）？

### 7.4 两个标准答案来源

- **`ast.h` 的字段**：定义“必须产出什么”；
- **`dump.cpp` 的打印内容**：定义“每个节点有哪些必要信息”，反向要求 builder 填对字段。
