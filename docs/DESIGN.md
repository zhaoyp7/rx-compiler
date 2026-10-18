# Rx 编译器设计文档

> 阶段一（AST + 语义）设计记录。本文件随阶段推进更新。
> 当前进度：P12（收尾）；official semantic 236/236，custom semantic 90/90。

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

## 8. 语义分析总体架构

### 8.1 数据流

```
.rx
 │ ANTLR RxLexer / RxParser
 ▼
ParseTree
 │ ASTBuilder
 ▼
Crate (AST)
 │ SemanticAnalyzer::run
 ▼
符号表 / 作用域 / TypeId / 布局 / 常量 / 表达式类型旁表
 │ 后续阶段
 ▼
LLVM IR → RV32IM
```

语义阶段不修改 AST，所有语义信息放在旁表：

- `bindings`：`PathExpr → symbol id`
- `exprTypes`：`Expr → TypeId`
- `structFieldTypes` / `structFieldMap`
- `structLayouts` / `layoutState`
- `constValues` / `constState`
- `fnParamTypes` / `fnReturnTypes` / `fnReceiverTypes`

### 8.2 核心数据模型

- `SymbolKind`：`Struct`、`Fn`、`Const`、`BuiltinFn`、`BuiltinType`、
  `Local`、`Param`、`AssocConst`、`AssocFn`
- `TypeKind`：`I32`、`U32`、`Isize`、`Usize`、`Bool`、`Unit`、`Never`、
  `Ref`、`Array`、`Struct`、`Box`、`Vec`
- `TypeArena` 对 `TypeInfo` 做 interning；`TypeId` 判等即类型判等
- `StructLayout` 保存字段偏移、总大小和对齐
- 函数签名表以 symbol id 为键：
  - `fnParamTypes` 包含 receiver（方法第一项是 `S` / `&S` / `&mut S`）
  - `fnReceiverTypes` 对关联函数是 `InvalidType`
  - `fnSymbols` 把 `FnDecl*` 映射到 symbol id
  - `derivedCloneSymbols` 保存 derive Clone 的合成方法 id

### 8.3 `run()` 顺序

```
clear state
registerBuiltins()
registerBuiltinSignatures()
collectCrate()            顶层 item + impl 关联项 + 定义处错误
resolveCrate()
  Pass A: struct 字段类型
  Pass A2: 所有函数/方法签名预扫描
  Pass B: 函数体、常量、impl 成员
checkLayouts()            所有 struct 的 size/align/offset + 递归布局
checkDerives()            Copy/Clone/PartialEq/Eq 能力与重复 derive
checkEntry()              main 签名
```

两遍扫描保证：

- 前向调用和相互递归可见；
- 方法 receiver 类型在函数体检查前可用；
- `Self` 在方法签名和体内解析到正确 struct；
- 结构体字段类型在布局和 derive 检查前完整。

### 8.4 名字、常量、类型

- 类型、值、字段三个命名空间分离；函数与常量共享值命名空间
- 一个 struct 的所有 impl 共享关联值命名空间
- 局部 `let` 初始化器在新绑定引入前求值，支持遮蔽
- 保护名：`i32`、`get_i32/print_i32/println_i32` 等按命名空间检查
- 常量求值只支持 `ConstInt/ConstBool/ConstPath/ConstNeg`；
  memo + visiting 检测直接/间接/关联常量环
- `resolveTypeId` 支持 struct、`Self`、`Box<T>`、`Vec<T>`、数组、引用；
  函数/struct/原始类型不接受类型实参，Box/Vec 恰好一个类型实参
- `resolveType` 仍用于 const 类型检查，路径解析与 `resolveTypeId` 共用大部分规则

### 8.5 表达式与控制流

- `infer(expr, expected)` 为每个表达式写 `exprTypes`
- expected 传播只穿过规范列出的位置：let 注解、赋值 RHS、调用实参、
  return/函数尾、struct 字段、数组 expected、if/loop 结果
- 不穿透一元/二元/借用运算
- `resolveBlock` 返回 block 类型：tail / unit / never
- 不可达代码仍做名字、类型、place 有效性检查；place 可变性违规按 UB 处理
- if 无 else 要求 unit；有 else 无 expected 时用 LUB
- loop break 值支持 expected 或 LUB；while 恒 unit
- `Never` 与任意返回类型兼容

### 8.6 结构体、数组、容器

- struct 字面量按 `structId` 查字段表，检查缺失/重复/未知/类型；
  结果类型是名义 struct 类型
- 字段访问反复解引用 `&/&mut/Box` 到 struct；未知字段报错
- 数组字面量 list 形式按 expected 或 LUB；repeat 形式 N>1 要求元素 Copy
- 索引基自动解引用 `&/&mut/Box` 到 Array/Vec；下标必须 usize
- Vec 索引隐式借用 Vec：赋值、复合赋值、可变重借、`&mut self`
  接收者都要求索引处对 Vec 的可变访问
- Box 解引用和固定数组索引不插入这种 Vec 借用
- 内建方法：
  - `Vec::<T>::new/len/is_empty/push/remove/clone`
  - `Box::<T>::new/clone`
  - 限定调用和点调用共享签名规则
- 容器 clone 要求元素 Clone；容器相等要求元素 PartialEq

### 8.7 方法与 derive

方法查找候选序列：

```
从接收者类型 t 开始：
  t, &t, &mut t
  若 t 是 &U / &mut U / Box<U>，继续以 U 为 t
  Vec 不产生解引用步骤
```

- 每个候选只匹配 receiver 类型完全相等的方法
- 参数个数、参数类型、接收者可变性不参与候选筛选
- 选中后再检查 `&mut self` 接收者是否可变访问
- 关联函数没有 receiver，不参与点调用
- 显式 `Type::method(receiver, ...)` 不做自动引用，receiver 作为普通第一个实参
- 派生 Clone 作为合成方法参与限定和点查找；与固有 clone 冲突是 UB，
  实现固定固有优先

derive 规则：

- Copy：字段全 Copy，且必须显式 Clone
- Clone：字段全 Clone；`&mut T` 不是 Clone
- PartialEq：字段全 PartialEq；操作数必须同源类型
- Eq：字段全 Eq，且必须显式 PartialEq
- 递归 struct 用 visiting 集合终止；`Vec<Self>` 可 Clone/PartialEq/Eq，但不能 Copy
- 重复 derive 在 builder 中记录 duplicate 标志，`checkDerives` 报错

### 8.8 引用、place 可变性、coercion、LUB

三个概念分开：

- `isPlace`：表达式是否表示存储
- `isMutablePlace`：能否直接写这个 place 的槽位
- `isMutableAccess`：能否以可变方式使用这个 place，包括使用其中存储的 `&mut`

辅助函数：

- `pathAllowsStoredMutRef`：到达一个 `&mut` 的路径是否被共享引用或
  Vec 索引阻断
- `hasMutableContainerAccess`：`Box<&mut Vec/Array>` 等容器基址是否
  能提供可变访问

关键区别：

- 不可变局部变量、struct、固定数组、拥有型 Box 中的 `&mut T`
  仍可使用其中的可变引用
- 路径穿过共享引用则阻断
- Vec 索引即使元素是 `&mut T`，也要求 Vec 本身可变
- Box<Vec<T>> 不可变所有者不能在 Vec 索引处提供可变访问

coercion：

- `&mut T → &T`
- 沿 `&/&mut/Box` 解引用到目标 pointee
- 可变目标要求外层 `&mut` 且路径无共享引用
- 不隐式借用 owned Box，不穿透 Vec
- `checkCoercion` 额外处理 `&mut` 重借：源表达式是 place 时检查
  `pathAllowsStoredMutRef`

引用 LUB：

- `&mut T` 与 `&T` 合并为 `&T`，两种顺序都支持
- 内层引用可变性不一致时失败
- `joinTypes` 用 `isCoercible` 实现，不再只接受完全相同类型

引用运算符：

- 算术/位运算最多一层共享引用
- 排序要求引用深度/可变性匹配；允许左 `&T`、右 `&mut T`
- 相等要求完全同源类型并检查 PartialEq

### 8.9 I/O 与入口

- `get_i32() -> i32`
- `print_i32(i32) -> ()`
- `println_i32(i32) -> ()`
- 内建 I/O 签名进入同一套 `checkCallArgs` 检查
- main 必须恰好一个、无参、无泛型、返回 unit

## 9. 明确不做的 UB 项

以下情况规范列为 UB 或测试保证不会出现，编译器不实现额外诊断：

- 所有权、借用检查、移动后使用、双重移动
- lifetime 有效性、引用有效性
- `use` 导入解析、别名、冲突
- 零大小数据（空 struct、零长数组的可观察使用）
- 函数作为值
- 省略 `Box::new` / `Vec::new` 的显式类型实参
- 跨源类型 `==` / `!=`
- 不可达代码中的 place 可变性违规
- 方法查找中同一候选的多个方法歧义

仍然必须诊断：

- 名字解析、遮蔽、重复定义、保护名
- 类型、expected、coercion、LUB、泛型实参数量
- 赋值/借用/`&mut self` 的 place 可变性
- 递归布局、derive 能力、main 签名、I/O 签名
- 不可达代码中的名字/类型/place 有效性

## 10. 测试与验收记录

### 10.1 阶段验收

| 阶段 | 验收目录/内容 | 结果 |
|---|---|---|
| P3 | entry、names-and-shadowing、namespace-errors、protected-names、constant-errors | 通过 |
| P4 | constants-and-paths、constant-errors | 通过 |
| P5 | recursive-layout | 6/6 |
| P6 | integer-arithmetic、shifts、casts-and-literals、boolean-and-short-circuit、compound-assignment | 通过 |
| P7 | blocks-if-and-never、loops-and-jumps、unreachable-checks | 通过 |
| P8 | structs-and-fields、arrays、expected-types、evaluation-order-and-temporaries | 通过 |
| P9 | methods-and-self、builtin-io、trait-dispatch、invalid-impls、calls | 31/31 |
| P10 | vec-index-mutability、box-and-moves、references-and-mutability、reference-coercions、reference-lub、scalar-reference-operators | 通过 |
| P11 | vec-operations、aggregate-arguments、recursive-traits、comprehensive-owned-tree | 通过 |
| P12 | `official:semantic` 全量 | 236/236 |
| P12 | `tests/custom` | 90/90 |

### 10.2 custom 测试目录

```
tests/custom/
├── names-and-entry/                 11
├── constants-and-layout/            11
├── expressions-and-control/         11
├── methods-and-derive/              18
├── references-and-mutability/       19
├── containers/                      17
└── io/                               3
```

运行：

```bash
make test FILTER=custom STAGE=semantic
make test FILTER=custom:containers STAGE=semantic
```

### 10.3 测试目录到规则的映射

| 目录 | 主要规则 |
|---|---|
| entry | main 签名、空参数、无泛型、unit 返回 |
| names-and-shadowing | 作用域、初始化器顺序、遮蔽 |
| namespace-errors | 类型/值/关联命名空间、重复定义 |
| protected-names | 内建保护名 |
| constants-and-paths | 常量求值、路径、环检测 |
| integer-arithmetic/shifts/casts | 运算符、字面量、cast |
| boolean-and-short-circuit | bool 运算与短路 |
| compound-assignment | 复合赋值目标与 RHS |
| blocks-if-and-never | 块类型、if、Never |
| loops-and-jumps | loop/while/break/continue |
| unreachable-checks | 不可达代码检查 |
| structs-and-fields | 结构体字面量、字段访问、布局 |
| arrays | 数组字面量、repeat、索引 |
| expected-types | expected 传播 |
| methods-and-self | 方法查找、receiver、关联调用 |
| builtin-io | I/O 签名 |
| trait-dispatch | derive、clone、eq、引用相等 |
| invalid-impls | impl 目标、泛型实参 |
| vec-index-mutability | Vec 索引可变访问传播 |
| box-and-moves | Box 访问与移动 |
| references-and-mutability | place 可变性、共享/可变引用 |
| reference-coercions | 引用 coercion |
| reference-lub | 引用 LUB |
| scalar-reference-operators | 引用算术/排序/相等 |
| vec-operations | Vec 方法全集 |
| aggregate-arguments | 聚合传参、返回、引用字段 |
| recursive-traits | 递归 derive |
| comprehensive-* | 综合一致性 |

### 10.4 目录到实现的索引

| 测试目录/主题 | 主要实现位置 |
|---|---|
| entry | `checkEntry`、`resolveFunction` |
| names-and-shadowing / namespace-errors | `collectCrate`、`resolvePath`、`lookupValue`、`Scope` |
| protected-names | `collectCrate`、`registerBuiltins` |
| constants-and-paths / constant-errors | `calcConstValue`、`calcConstSymbol`、`resolveConstPath`、`resolveTypeId` 的数组长度分支 |
| integer-arithmetic / shifts / casts / boolean | `infer` 的 `BinaryExpr`、`UnaryExpr`、`CastExpr` 分支 |
| blocks-if-and-never / loops-and-jumps | `resolveBlock`、`resolveStmt`、`infer` 的 if/loop/while/break/return 分支 |
| unreachable-checks | `resolveBlock` 的 `diverged`、`resolveStmt` 的不可达路径 |
| structs-and-fields | `resolveTypeId`、`layoutStruct`、`infer` 的 `StructLit`/`FieldAccessExpr` 分支 |
| arrays | `infer` 的 `ArrayLit`/`ArrayRepeat`/`IndexExpr` 分支、`isCopyType` |
| expected-types | `infer` 的 `expected` 参数与各 coercion site |
| methods-and-self | `resolveSignatures`、`resolveSignature`、`findUserMethod`、`resolvePath`、`infer` 的 `MethodCallExpr` 分支 |
| builtin-io / calls | `registerBuiltinSignatures`、`checkCallArgs`、`infer` 的 `CallExpr` 分支 |
| trait-dispatch / copy-clone | `checkDerives`、`supportsClone`、`supportsPartialEq`、`supportsEq`、`isCopyType`、`getDerivedClone`、`infer` 的 Eq/Ne 分支 |
| invalid-impls / 泛型实参 | `collectCrate`、`resolveTypeId`、`resolvePath`、`infer` 的 MethodCall 泛型实参检查 |
| references-and-mutability | `isPlace`、`isMutablePlace`、`isMutableAccess`、`pathAllowsStoredMutRef`、`hasMutableContainerAccess` |
| reference-coercions / reference-lub | `isCoercible`、`checkCoercion`、`joinTypes`、各 coercion site |
| scalar-reference-operators | `infer` 的算术/排序/相等分支、`checkOrderingOperands` |
| vec-index-mutability / vec-operations | `isMutableAccess` 的 Index 分支、`hasMutableContainerAccess`、`infer` 的 Vec 内建方法分支 |
| aggregate-arguments-and-reference-fields | `resolveFunction` 参数绑定、`checkCallArgs`、`infer` 的聚合表达式 |
| recursive-traits | `supportsClone/PartialEq/Eq` 的 visiting、`checkDerives` |
| comprehensive-* | 以上规则组合；无单独特判 |

## 11. 已知简化与 Review 说明

- `SemanticAnalyzer::error()` 目前只置 `failed`，不保存语义错误消息；
  reject 用例只检查退出码。这是当前阶段的简化，后续可加诊断文本。
- `InvalidType` 作为 wildcard：签名/类型解析失败时避免级联错误。
  正向用例必须靠 custom 用例确认不是 wildcard 假绿。
- `resolveType` 和 `resolveTypeId` 仍有两套路径检查；
  后续可收敛为单一类型解析入口。
- `resolveFunction` 仍会重新解析参数/返回类型；签名表已经可用，
  后续可优先查表减少重复。
- 生成 ANTLR parser 已提交到 `src/generated/`，OJ 不需要 Java。
- OJ 构建使用 `-j2`，避免 cc1plus OOM；本地可临时加大。

## 12. Review 演示命令

```bash
# AST 结构
./build/rxc --stage ast tests/official/semantic/methods-and-self/acc-receiver-auto-borrows-and-repeated-box-reference-dereferences.rx

# accept
./build/rxc --stage semantic tests/official/semantic/comprehensive-owned-tree/acc-clone-mutate-remove-tree.rx
echo $?   # 0

# reject：不可变 Vec 中的 stored &mut
./build/rxc --stage semantic tests/official/semantic/vec-index-mutability/rej-write-through-stored-reference.rx
echo $?   # 1

# 全量回归
make test FILTER=official:semantic STAGE=semantic
make test FILTER=custom STAGE=semantic
```
