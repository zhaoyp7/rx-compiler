# Rx 编译器设计文档

> 记录 AST、语义分析与 LLVM IR 的实现设计。
> 第 1–12 节保留前端设计与历史验收记录；第 13 节说明当前 IR 实现及后端衔接约定。

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

`accept(AstVisitor&)` 提供双分派接口，`DumpVisitor` 使用该接口遍历 AST。
当前语义分析与 IR lowering 通过 `dynamic_cast` 分发节点；新增节点时，需要同步检查
这两层的处理入口，不能只依赖 visitor 的纯虚函数检查覆盖率。

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
| P12 | `tests/custom/semantic` | 90/90（P12 历史验收记录） |

### 10.2 custom 测试目录

```
tests/custom/
├── semantic/
│   ├── names-and-entry/
│   ├── constants-and-layout/
│   ├── expressions-and-control/
│   ├── methods-and-derive/
│   ├── references-and-mutability/
│   ├── containers/
│   └── io/
└── codegen/
```

原有语义用例迁移到 `semantic/` 时保留原内容，后续语义回归也加入该目录。运行用例放在
`codegen/`，由 runner 同时用于 IR 和后端阶段；覆盖范围与预期结果说明见
[`tests/custom/README.md`](../tests/custom/README.md)。

运行：

```bash
make test FILTER=custom:semantic STAGE=semantic
make test FILTER=custom:semantic:containers STAGE=semantic
make test FILTER=custom:codegen STAGE=ir
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

## 13. LLVM IR 设计

### 13.1 编译流程与职责

IR 阶段读取已经通过语义分析的 AST 和旁表，不再推导名字、方法候选、类型布局或
derive 能力。`src/main.cpp` 的 `--stage ir` 分支先构建 AST、执行语义分析，
再调用 `IrGenerator::generate(std::ostream&)`，将 LLVM IR 文本写入 `-o` 指定的文件。

```text
Crate + SemanticAnalyzer 旁表
            │
            ▼
IrGenerator + TypeMapper + IrBuilder
            │
            ▼
Module → Function → BasicBlock → Instruction
            │ validate / print
            ▼
           .ll
            │ clang-22（IR 阶段验证）
            ▼
      RV32IM 汇编 + runtime.s → REIMU → stdout 比对
```

| 文件 | 职责 |
| --- | --- |
| `src/ir/ir.h` / `ir.cpp` | IR 数据结构、基本结构校验、LLVM 文本输出、机器入口 wrapper |
| `src/ir/builder.h` / `builder.cpp` | 创建指令、临时值、基本块和入口块中的栈槽 |
| `src/ir/type.h` / `type.cpp` | `TypeId` 到 LLVM 类型的映射、结构体和 Vec 类型声明 |
| `src/ir/generator.h` / `generator.cpp` | AST lowering、引用转换、控制流、容器、clone/eq |
| `runtime/runtime.c` | I/O 与堆分配的 C ABI 实现 |
| `config.mk` / `scripts/test.py` | 构建 runtime、调用编译器、Clang 和 REIMU，检查输出 |

所有源函数都进入实际 lowering；失败会返回错误并让驱动退出 1。当前实现没有
`supports*` 预筛选，也不会将无法处理的函数替换为空函数或仅留下声明。

### 13.2 目标与类型表示

模块固定输出：

```llvm
target triple = "riscv32-unknown-elf"
target datalayout = "e-m:e-p:32:32-i64:64-n32-S128"
```

目标为 RV32IM / ILP32。`TypeMapper::valueType()` 描述表达式值、参数和返回值的
LLVM 类型，`storageType()` 描述源变量、字段、数组元素和堆内存中的表示。

| Rx 类型 | LLVM 值表示 | 存储表示 | 大小 / 对齐（字节） |
| --- | --- | --- | --- |
| `i32`、`u32`、`isize`、`usize` | `i32` | `i32` | 4 / 4 |
| `bool` | `i1` | `i8` | 1 / 1 |
| `&T`、`&mut T`、`Box<T>` | `ptr` | `ptr` | 4 / 4 |
| `[T; N]` | `[N x storageType(T)]` | 同左 | `N * size(T)` / `align(T)` |
| 结构体 `S` | `%struct.S` | 同左，字段使用存储类型 | 语义层计算的布局 |
| `Vec<T>` | `%struct.Vec` | 同左 | 12 / 4 |
| `()`、`!` | `void` | 不分配可观察的数据存储 | 0 / 1 |

`void` 用于函数签名；unit 和 Never 都不产生普通表达式值，但控制流状态不同，
由 `LowerResult` 区分。聚合值保留字段的存储表示，例如 bool 数组的值类型也是
`[N x i8]`，不会转换成 `[N x i1]`。

`toStorageValue()` 在 bool 写入时执行 `zext i1 → i8`；`fromStorageValue()` 在读取
时执行 `trunc i8 → i1`，其他类型原样通过。显式源级 cast 由表达式 lowering 单独处理。

结构体字段按声明顺序布局，并满足字段对齐及尾部对齐；数组元素连续排列。
分配大小与对齐读取 `SemanticAnalyzer::typeSize()` / `typeAlign()`，其中结构体使用
已计算的 `StructLayout`。字段、数组和 Vec 元素地址使用 GEP，元素步长由 LLVM 类型确定。

### 13.3 内存 IR 与构建上下文

`Module` 保存命名类型和函数；`Function` 保存签名、声明标记和基本块；
`BasicBlock` 保存标签和有序指令。当前指令子集为：

```text
RET  CALL  ALLOCA  LOAD  STORE  BINARY  ICMP  CAST  BR  UNREACHABLE  GEP
```

`Instruction` 用 `kind/result/type/op/operands` 描述指令，值名、类型和操作数仍使用
字符串；CALL 参数和 GEP 索引中包含显式的 LLVM 类型文本。它是 LLVM 子集的内存表示，
目前没有独立的 Value 对象或通用 LLVM 文本解析器。

`IrBuilder` 管理当前基本块以及函数内唯一的 `%tN`、`%tmp.N` 和标签编号。
`LowerContext` 保存 builder、类型映射、语义分析器、当前返回类型、参数 SSA 值和循环栈。
循环上下文记录 break/continue 目标、结果槽位和结果类型。

### 13.4 LowerResult 与表达式入口

| 返回形式 | `ok` | `terminated` | `hasValue` | 含义 |
| --- | --- | --- | --- | --- |
| `failResult()` | false | false | false | lowering 失败 |
| `unitResult()` | true | false | false | 正常完成，无数据值 |
| `valueResult(v)` | true | false | true | 正常完成，结果为 `v` |
| `terminatedResult()` | true | true | false | 当前路径已结束，不应继续发射指令 |

调用子表达式后，应先传播失败和 `terminated`，再检查需要的数据值及类型转换。
例如 `return` 出现在实参或容器初始化中时，外层不得继续发出 call、分配或 store。
这里的 `terminated` 表示当前路径结束；`break`、`continue` 也返回该状态，并不代表整个
源函数已经返回。

| 入口 | 返回结果的含义 |
| --- | --- |
| `lowerExpr()` | 普通表达式值；统一分发调用、赋值、块及控制流表达式 |
| `lowerPlace()` | 可寻址位置的地址；不自动将该位置 load 成普通值 |
| `lowerPlaceOrTemp()` | 优先使用已有位置；值表达式需要地址时保存到临时槽位 |
| `lowerAddress()` | 为字段、索引和接收者定位地址，按当前操作需要穿过 Ref/Box |
| `lowerExprToType()` | 按语义层确定的期望类型 lowering，并处理允许的引用转换 |
| `lowerReceiverValueToType()` | 接收者求值一次，逐层读取到已确定的 Self 类型 |

`lowerStmt()` 只处理 let 和表达式语句。表达式语句统一调用 `lowerExpr()`，传播失败和
提前终止状态，并丢弃正常结果。`lowerBlock()` 顺序处理语句，最后将期望类型传给块尾
表达式。赋值和复合赋值的语义结果为 unit，因此既可作为带分号的语句，也可作为块尾。

### 13.5 栈槽、参数与聚合值

源变量槽位统一由 builder 的 `symbolId → slot` 映射管理，`ensureSlot()` 保证同一符号
只创建一个槽位。变量读取先查槽位并 load，再查 `paramValues` 中的参数 SSA 值；
这个顺序保证可变参数修改后读取到最新值。当前不再维护单独的 `paramSlots` 映射。

可变参数，以及结构体、数组、Vec 参数，在函数入口复制到自己的槽位；其他参数通常
直接使用传入的 SSA 值。变量槽位使用存储类型，bool 参数进入槽位时也需执行 `i1 → i8`。

所有 `ensureSlot()` / `ensureTemp()` 产生的 alloca 都插入入口块，位于已有终止指令之前。
控制流结果槽位属于编译器内部临时量，可以使用 `valueType()`，例如 bool 合流槽位为
`i1`；这与源变量或字段使用 `i8` 不冲突。

结构体与数组按值传参、返回和赋值，数组不退化为指针。构造聚合值时，先创建临时存储，
按字段或元素写入，再 load 整个聚合值。结构体初始化按源码字段出现顺序求值，字段地址
仍按声明布局计算；`[expr; N]` 只求值一次 expr，再复制已得到的值。
Copy/移动和 repeat 的合法性由语义层检查；普通按值传递不会隐式调用 clone。

### 13.6 期望类型、引用与求值顺序

let 初始化、赋值 RHS、函数和方法实参、字段和数组元素、Box/Vec 插入值、return、
if 分支和带值 break 均在对应位置传入期望类型。`LowerContext::returnTypeId` 保存当前
源函数的返回类型，使显式 return 和函数体尾表达式使用同一套转换规则。

`coerceValueToType()` 在正常求值后先处理无期望类型及同类型情况，其余转换限于语义层
已经批准的引用 coercion。需要穿过内层 Ref/Box 时，逐层 load 指针；共享性差异若不改变
物理表示，则不生成额外指令。块表达式直接把期望类型传给 `lowerBlock()`，避免在内部
完成转换后，再按原始块类型转换一次。

接收者转换以方法已确定的 Self 为终点。例如 clone 需要保留引用结果时，不能把所有
引用层都无条件剥离。共享引用的 clone 复制引用本身，Box 的 clone 才递归复制拥有值。

普通二元表达式先求值 LHS，再求值 RHS；赋值和复合赋值先求值 RHS，再计算目标地址，
复合赋值在此之后读取目标旧值。引用形式的标量运算分别处理左右两侧的解引用，两个
判断不能改成互斥的 `if/else if`。移位量显式与 31 做按位与，整数运算不添加
`nsw` / `nuw` 标记，以保留规定的回绕行为。

### 13.7 控制流与合流

if 和有值 loop 使用入口块中的结果槽位合流，不生成 phi。各个正常到达的分支先把
转换后的结果写入槽位，再跳转合流块；已经 return/break/continue 的分支不补跳转。
两个 if 分支均终止时，整个 if 返回 `terminatedResult()`，无需创建可达的合流块。

`&&` / `||` 使用条件分支，仅在需要时进入 RHS 块，并通过 `i1` 临时槽位取得结果。
while 的 continue 目标是条件块；loop 的 continue 目标是循环体入口。带值 break
按循环结果类型转换，写入结果槽位后跳转出口。无出口的 Never loop 不生成正常结果。

return 先处理返回表达式；如果该表达式本身已终止，直接传播状态，避免重复 ret。
语义分析仍检查不可达源代码，IR lowering 可以在当前路径终止后停止生成后续语句。

### 13.8 函数命名、入口与调用约定

| 源函数或辅助函数 | LLVM 符号 |
| --- | --- |
| 普通函数 `f` | `_rx_fn_f` |
| 结构体 `S` 的方法或关联函数 `f` | `_rx_m_S_f` |
| 源 `main` | `_rx_main` |
| 机器入口 wrapper | `main` |
| `S` 的派生 clone / equality | `_rx_clone_S` / `_rx_eq_S` |

方法接收者作为显式第一参数传递，按值接收者保留值表示，引用接收者传 `ptr`。
I/O 和分配辅助函数使用固定的外部符号。

机器入口的行为为：

```llvm
define i32 @main() {
entry:
  call void @_rx_main()
  ret i32 0
}
```

源代码对 main 的调用，包括递归调用，目标都是 `_rx_main`，其返回只恢复源调用者。
IR 阶段由 Clang 将函数签名映射到 RV32IM 调用约定；当前 IR 保留按值聚合签名，尚未
实现自研后端的寄存器分配、栈帧和聚合参数机器级传递。

### 13.9 Box、Vec 与堆内存

Box 的值是指向 T 存储的 `ptr`。构造时先求值并转换内容，再申请 `size(T)`、
`align(T)` 对应的空间，将内容按存储类型写入。

所有 Vec 实例共用以下 LLVM 命名类型，元素类型由语义 TypeId 和 GEP 保留：

```llvm
%struct.Vec = type { ptr, i32, i32 }
```

| 字段 | 索引 | 字节偏移 | 含义 |
| --- | --- | --- | --- |
| data | 0 | 0 | 连续元素缓冲区地址 |
| len | 1 | 4 | 已初始化元素数量 |
| cap | 2 | 8 | 可容纳元素数量 |

`new()` 返回 `{null, 0, 0}`，不申请元素缓冲区；`len()` 读取 len，`is_empty()` 比较
len 是否为零。索引对 data 使用元素存储类型的 GEP，不携带切片长度或 lifetime 数据。

push 的插入值先完成求值和类型转换，再修改容器：

1. `len < cap` 时直接复用缓冲区。
2. 需要扩容时，零容量取 4，非零容量翻倍；分配 `newCap * size(T)` 字节。
3. 旧 len 非零时，用 memmove 移动 `len * size(T)` 字节，随后更新 data/cap。
4. 在 data[len] 写入新值，最后更新 len。

扩容移动已有元素不会调用 clone。remove 先读取待移出的值，再用 memmove 将后续
元素左移，最后将 len 减一并返回原值；cap 保持不变，删除尾元素时跳过移动。
这里可能出现重叠源/目标区域，因此使用 memmove。

### 13.10 clone、相等比较与递归 derive

`lowerCloneValue()` 根据具体源类型生成复制行为：

| 类型 | clone 行为 |
| --- | --- |
| 整数、bool | 直接返回值 |
| 合法可克隆的共享引用 | 复制地址，不复制引用目标 |
| Box | clone 内容，再分配并初始化新的拥有存储 |
| Vec | 非空时按 len 分配缓冲区，运行时循环逐元素 clone；结果 cap 等于 len |
| 数组 | 按固定长度展开元素 clone |
| 结构体 | 调用 `_rx_clone_S(ptr self) -> %struct.S`，逐字段 clone |

空 Vec 的 clone 保持 data 为 null、len/cap 为零，不分配缓冲区。支持哪些类型的
clone 由语义层决定；lowering 中的指针表示不扩大语言允许的 Clone 能力。

派生相等辅助函数签名为 `_rx_eq_S(ptr lhs, ptr rhs) -> i1`。结构体和数组比较字段或
元素值，不读取 padding 进行整体字节比较。Box 比较内容；引用相等按语言要求比较
目标值；Vec 先比较长度，再按顺序比较元素，遇到不等即退出。`!=` 对相等结果取反。

递归或相互递归的结构体可能通过 Box/Vec 在运行时形成有限的拥有树。生成阶段先收集
全部派生函数，为 Module 的函数 vector 预留容量，再先创建一组辅助函数，后填函数体。
clone 组完成后处理 equality 组，避免生成函数体时递归展开结构体类型，同时保证保存的
`Function*` 不因 vector 扩容失效。运行时递归在空 Vec 等实际数据边界结束。

### 13.11 Runtime 边界

生成模块声明以下接口：

```llvm
declare i32 @get_i32()
declare void @print_i32(i32)
declare void @println_i32(i32)
declare ptr @__rx_alloc(i32, i32)
declare ptr @memmove(ptr, ptr, i32)
```

`runtime/runtime.c` 通过 scanf/printf 实现 I/O，通过 malloc 实现 `__rx_alloc(size, align)`；
当前 align 参数未参与分配计算。生成代码负责计算字节数和 1 或 4 字节的对齐，
memmove 由 REIMU 提供。runtime 不定义 main，也不解释源程序或 IR。

`config.mk` 的 BUILD 将 C runtime 编成 RV32IM/ILP32 汇编，测试 runner 与程序汇编
一起交给 REIMU。I/O、分配和内存移动跨越外部 C ABI，后续自研后端必须保留这些接口，
并满足 psABI 的参数、返回值、寄存器保存和栈对齐要求。

按照[后端与堆分配契约](../rx-spec-zh/16-backend/16.00-backend.md)，有效分配计算适合
usize，不需要额外的动态溢出检查；允许保留旧缓冲区，不要求实现 free 或递归析构。
索引和 remove 的有效性依赖语言规定的有效测试前提，当前不生成边界检查或 panic。

### 13.12 校验与测试合同

`BasicBlock::validate()` 要求标签和指令非空，且恰好最后一条是终止指令。
`Function::validate()` 检查签名及函数体基本块；`Module::validate()` 遍历函数检查。
这些是基本结构检查，目前不完整验证操作数类型、SSA 定义与支配关系、标签引用或调用签名。
例如把 ptr 误用为 i32 操作数，仍需由 Clang 的 LLVM IR 检查发现。

`Module::print()` 校验后依次输出 target、命名类型、声明和定义。测试 runner 从
`stage=codegen` 的 manifest 条目自动派生 IR 测试，依次检查：编译器成功生成文件、
Clang 接受该 IR、REIMU 执行成功，以及 stdout 与预期逐字节相同。

```bash
# 语义回归
make test STAGE=semantic

# 全量 IR 与运行结果验证
make test STAGE=ir

# 针对某个规则定位问题
make test FILTER=custom:codegen:expression-dispatch STAGE=ir
make test FILTER=official:codegen:scalar-reference-operators STAGE=ir
```

测试用例只保存源码、manifest 和确定的输入/预期输出，运行日志与中间产物由 runner
写入 `target/tests/`。目录说明见 [custom README](../tests/custom/README.md)。

### 13.13 当前简化与后端衔接

当前没有 phi 或寄存器分配，合流与可变变量依赖栈槽；Vec clone 仍逐元素处理，
固定长度数组的 clone/eq 仍展开生成；派生 clone/eq 辅助函数即使没有调用也会生成。
这些都是当前实现选择，后续优化必须保留值语义和求值顺序。

`IrGenerator::generate()` 目前在函数内构建 Module 并直接打印，还没有独立的
`buildModule()` 接口。衔接自研后端时，应先分离模块构建与文本输出，再明确内存 IR 的
类型和操作数访问接口，保留 `.ll` 输出作为验证手段。最终程序汇编必须由自研后端生成，
Clang 可以继续用于 IR 验证和 C runtime 编译。

后端需要延续 bool 的读写转换、结构体/数组/Vec 布局、源与机器 main 的分离、
按值聚合语义及 runtime ABI。机器级调用约定可以单独设计，但外部边界必须符合
RV32IM/ILP32 psABI；不能直接把 LLVM 聚合值当成一个 32 位寄存器。
