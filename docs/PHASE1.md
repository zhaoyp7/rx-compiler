# 阶段一（AST + 语义）执行手册

> 对应第 4 周 AST 验收；总目标：`make test FILTER=official:semantic STAGE=semantic` 全绿（236 例 / 45 目录）。
> 已按 2025 版规范修订：`use`/lifetime 可丢弃；不需要所有权/借用/lifetime 检查器。
> 编写约定：每个任务给出 **做法**（具体怎么下手）和 **作用**（为什么需要它、它影响后面什么）。
> 编译器代码自己写；本手册只描述方法和原因，遇到设计取舍卡壳再来讨论。

---

## P0 工程骨架

**为什么先做这一步**：ANTLR 工具链、构建系统、测试接线没打通之前，后面任何语义代码都无法被执行和验证；本阶段产出的 `build/rxc` 与 `config.mk` 是之后 12 个阶段唯一的运行入口。

1. **备份参考配置**
   - 做法：`cp config.mk config.reference.mk`，之后把 `config.mk` 改成你自己的命令。
   - 作用：随时切回 rustc 参考实现生成“标准答案”的 `.ll`/`.s` 对照调试；改坏 `config.mk` 也能一键恢复。

2. **准备 ANTLR 工具 jar**
   - 做法：`mkdir -p tools && curl -fsSL -o tools/antlr-4.13.2-complete.jar https://www.antlr.org/download/antlr-4.13.2-complete.jar`。
   - 作用：把 `grammar/*.g4` 生成 C++ 词法/语法分析器。用 4.13.2 是因为文法注释明确该版本；系统已有 C++ runtime 4.13.1，实测兼容。

3. **写 `scripts/gen_parser.sh`**
   - 做法：复制两份 g4 到临时文件；用 sed 把 `lexer grammar Lexer;` 改成 `RxLexer`、`parser grammar Parser;` 改成 `RxParser`、`tokenVocab=Lexer` 改成 `RxLexer`；再运行 `java -jar tools/antlr-4.13.2-complete.jar -Dlanguage=Cpp -visitor -no-listener -o src/generated <两个临时 g4>`。
   - 为什么要改名：C++ 下生成类 `Lexer`/`Parser` 会与 `antlr4::Lexer`/`antlr4::Parser` 在 `using namespace antlr4;` 里重名（实测编译报 ambiguous）；改名是 C++ target 的常规绕法。
   - 作用：官方 g4 更新后重跑一次即可，避免手工改动和漏改。

4. **写 `CMakeLists.txt`**
   - 做法：建一个可执行目标，编译 `src/**/*.cpp` 和 `src/generated/*.cpp`；include 目录加 `src/generated`、`/usr/include/antlr4-runtime`；链接 `antlr4-runtime`；输出到 `build/rxc`。
   - 作用：统一构建入口，`config.mk` 的 `BUILD` 只调它；测试 runner 只认 `build/rxc`。

5. **写 `src/main.cpp`：CLI、解析、错误闸门**
   - 做法：解析 `--stage <stage>`、`-o <out>`、输入文件；用 `ANTLRInputStream` → `RxLexer` → `CommonTokenStream` → `RxParser`，调用入口规则 `crate()`；自定义 `BaseErrorListener` 收集语法错误。只有错误数为 0 才继续建 AST；语法/语义错误一律 exit 1，成功 exit 0。
   - 为什么：测试合同要求 reject 用例“恰好 exit 1”；而 ANTLR 默认会做错误恢复并继续，如果不停下会基于残缺的树产出错误 AST。
   - 作用：后续每个 stage 都复用这套入口和退出码约定。

6. **改 `config.mk` 接上测试**
   - 做法：
     ```make
     BUILD = cmake --build build -j32 >/dev/null && : > {runtime}
     SEMANTIC = ./build/rxc --stage semantic {source}
     IR / CODEGEN：先留参考配置或留空，第 8/12 周再接
     ```
   - 作用：`make test` 的每条命令都从 `config.mk` 读取；这一步把测试运行器接到你的编译器上。

7. **跑通调用链**
   - 做法：先写一个 `--stage semantic` 恒返回 1 的桩；跑 `make test FILTER=official:semantic:entry STAGE=semantic`，确认 runner 能构建、调用、读取退出码并出报告。
   - 作用：在写语义逻辑前先排除环境问题，避免以后把“没接上线”误判成“语义写错了”。

**验收**：`cmake --build build -j32` 一次成功；写个小脚本遍历 semantic 全部源码跑解析，零语法错误。
**常见坑**：退出码必须精确 0/1（异常、signal、超时都会判失败）；`BUILD` 必须留下合法的 `{runtime}` 文件（空文件也要创建，≤8MiB）；ANTLR 生成物和 `build/` 不要提交。

---

## P1 AST 定义

**为什么做**：parse tree 冗长且保留消歧用的影子规则，不适合后续使用；AST 是规范化后的唯一数据接口，P2 之后的每个阶段都只面对它。AST 设计不好，P2 会反复返工。

1. **定义 `SourceSpan`**
   - 做法：结构体记录起止偏移和行列；从 token 的 start/stop 取。
   - 作用：所有错误信息定位、dump、Code Review 展示都靠它。

2. **确定节点基类与内存管理**
   - 做法：一个 `Node` 基类（带 span、虚析构）+ `std::unique_ptr` 持有子节点；或纯 `std::variant` 方案二选一。
   - 作用：限定树的所有权模型，避免后期内存问题；这是架构上最重要的一次决定。

3. **item 节点**
   - 做法：`UseDecl`（可只留空节点，因为解析后要丢弃）、`FnDecl`（名称、参数、返回类型、body、泛型/lifetime 占位）、`StructDecl`（字段、derive 列表）、`ConstDecl`、`ImplDecl`（目标类型、成员）。
   - 作用：P3 的符号收集直接遍历这五个类型。

4. **TypeSyntax 节点**
   - 做法：`UnitType`、`ParenType`、`PathType`（路径段 + 泛型实参）、`RefType`（mut、双重引用要能区分）、`ArrayType`（元素 + 长度表达式）。
   - 作用：P5 把它们解析成语义类型；此处只做语法结构，不要提前解析。

5. **语句节点**
   - 做法：`LetStmt`（mut、名字、可选类型、初值）、`ExprStmt`（表达式 + 是否有分号）；单独的 `;` 直接丢弃。
   - 作用：P7 类型检查的基本单位；块尾表达式另行存在 `BlockExpr` 里。

6. **表达式节点（全集）**
   - 做法：字面量（整数带后缀）、`PathExpr`、`StructLit`、`ArrayLit`（列表/重复两种）、`Call`、`MethodCall`、`FieldAccess`、`Index`、一元（`- ! * & &mut && &&mut`）、二元、`Assign`（含复合运算符）、`Cast`、`If`、`Loop`、`While`、`Break/Continue/Return`、`Block`（stmts + tail）。
   - 作用：P2 的映射目标、P6 的遍历对象；字段设计时想清楚“这个节点在 P6 需要哪些信息”。
   - 注意：`&&x` 在文法里是 `ANDAND`，语义是**两个引用**，不是逻辑与。

7. **ConstValue 节点**
   - 做法：整数、布尔、路径、取负、括号。
   - 作用：P4 常量求值器只处理这一小组节点。

8. **两个提前决策**
   - 做法：① 此时不要给表达式挂类型（P5/P6 再做）；② place/普通表达式先不区分（P6 从形状判断左值）。
   - 作用：避免过早耦合，减少 P1 返工。

**验收**：拿文法逐条产生式对照节点清单，确认无遗漏；随便挑 10 行程序能手工画出 AST。
**常见坑**：`as` 可以链式（`x as i32 as u32`）；结构体字面量与块有歧义（文法已处理，AST 只接结果）；块尾表达式与语句要分开放。

---

## P2 AST 构建

**为什么做**：把 parse tree 一一映射成 AST，同时丢弃 `use`/lifetime、归一化 `condition*`/`statement*`/`closed*` 等影子规则。这一步的规范化程度，直接决定 P3-P11 的工作量。

1. **Visitor 基础设施**
   - 做法：实现 `RxParserBaseVisitor` 的子类；用 `std::any` 包装返回值，写一个 `template<T> T get(antlr4::tree::ParseTree*)` 辅助函数做带检查的 `any_cast`。
   - 作用：复用 ANTLR 生成的访问方法，避免手写遍历。

2. **item 与 TypeSyntax 下降**
   - 做法：每个 visitXxx 返回对应节点的 `unique_ptr`；函数体、结构体字段逐层下降。
   - 作用：P3 能遍历到完整顶层信息。

3. **表达式下降：掌握三种模板**
   - 左折叠：`additiveExpression: mul (op mul)*` → 先 visit 第一个，再依次和 op/rhs 组成 `Binary`。
   - 后缀循环：`postfixExpression: primary postfixSuffix*` → 从 base 开始，每个后缀套一层 Call/Index/Field/MethodCall。
   - 影子规则归一：`condition*`/`statement*`/`closed*`/`conditionBreak*` 全部映射到与普通表达式相同的 AST 节点。
   - 作用：AST 里没有“条件表达式”这种特殊种类，P6 只写一套规则。

4. **语句与块下降**
   - 做法：块 = `stmts` 列表 + 可选尾表达式；`;` 产生的空语句丢弃；`if` 的 else if 链递归下降。
   - 作用：P7 的类型规则依赖“尾表达式”这个结构，别把它并进语句列表。

5. **丢弃 `use` 与 lifetime**
   - 做法：`use` 直接返回空；lifetime 参数/标注/实参/bound 解析时跳过。
   - 作用：规范明确这两类不需要语义处理，保留只会给 P3 制造无意义的错误。

6. **错误闸门**
   - 做法：任何语法错误 → 不构建 AST，直接让上层 exit 1。
   - 作用：防止基于残缺树做语义分析产生假报错/假通过。

7. **`--stage ast` 打印器**
   - 做法：稳定缩进格式（节点名、关键字段、叶子值）。
   - 作用：这是你在 P3-P12 最常用的调试工具，也是 Code Review 的演示材料；从第一天就做好。

**验收**：挑 5 个覆盖不同语法的用例（结构体、方法、数组、控制流、引用）人工核对 dump；全量源码跑 AST 构建无崩溃。
**常见坑**：`any_cast` 类型不对会抛异常且信息难懂，统一走辅助函数；路径节点里 `Self`/泛型实参别丢；`Match` 之类的保留字应该在解析阶段就报错（Rx 不支持）。

---

## P3 符号与名字解析（目标 39 例）

**为什么做**：类型检查前必须把每个名字解析成“局部变量 / 函数 / 结构体 / 常量 / 错误”。本阶段覆盖 `entry`、`names-and-shadowing`、`namespace-errors`、`protected-names`。

1. **全局符号表 + 两遍扫描**
   - 做法：第一遍只收集顶层 item（函数签名、结构体、常量、impl 方法），第二遍再解析类型和函数体。
   - 作用：支持前向引用和互相递归（`f` 调 `g`、`g` 调 `f`），否则必须先定义后使用，与 Rust 不符。

2. **作用域链**
   - 做法：`Scope { parent; map<name, Symbol> }`；进入块压栈、退出弹栈；`let` 遮蔽就是内层 map 覆盖外层。
   - 作用：`names-and-shadowing` 的接受/拒绝都靠它；也是 P6 查变量类型的入口。

3. **重复定义检查**
   - 做法：同一 Scope 内重名（函数、结构体、常量、impl 方法）报错；区分“遮蔽”（允许）和“重复定义”（拒绝）。
   - 作用：`namespace-errors` 16 个 reject 的大头。

4. **路径解析**
   - 做法：标识符按 局部 → 参数 → 全局 顺序查；支持 `Self`；函数/结构体/常量引用要能区分。
   - 作用：P6 调用表达式和结构体字面量都依赖解析结果，建议把解析结果直接记在 AST 节点上（如绑定到符号 id），避免重复查找。

5. **内建符号**
   - 做法：`print_i32`/`println_i32`/`get_i32` 按拼写识别为内建函数；`Box`/`Vec` 识别为内建类型名。
   - 作用：P8/P9 对它们的成员和构造做特判；测试里 `use rx::core::*` 已丢弃，名字是否可用只取决于拼写。

6. **保护名检查**
   - 做法：`u32/isize/usize/bool/Box/Vec/Copy/Clone/PartialEq/Eq` 不能作 struct 名；`get_i32/print_i32/println_i32` 不能作局部绑定；在定义处报错。
   - 作用：这是规范里明确要求实现的检查（不是 UB），漏了会挂 accept/reject。

7. **`main` 签名检查**
   - 做法：必须恰好有一个顶层 `main`，无参数、无泛型、返回 `()`。
   - 作用：`entry` 5 个用例；也是后面 IR 阶段生成入口 wrapper 的前提。

**验收**：`make test FILTER=official:semantic:entry,official:semantic:names-and-shadowing,official:semantic:namespace-errors,official:semantic:protected-names STAGE=semantic` 全绿。
**常见坑**：把遮蔽误判成重复定义；对已丢弃的 `use` 报“未定义导入”；impl 方法的重名检查和普通函数混在一起做。

---

## P4 常量求值（含在 P3 的 39 例内）

**为什么做**：数组长度、`[e; N]` 的重复次数、`const` item 的值都必须编译期算出来；P5 的结构体布局也依赖常量。

1. **求值器接口**
   - 做法：输入 `ConstValue` 节点，能查全局常量；返回 `{类型, int64 或 bool}`；类型限定在整数/布尔。
   - 作用：P5 解析 `[T; N]` 时直接调用。

2. **memo 与环检测**
   - 做法：对常量名做 DFS，记录 visiting/finished；检测 `const A = B; const B = A;`。
   - 作用：防止死循环并正确报错（`constant-errors` 有相关用例）。

3. **接入所有常量上下文**
   - 做法：数组类型长度、数组重复次数、const item 初值；这几处的报错归属该上下文。
   - 作用：把“哪里允许常量表达式”和“求值本身”解耦，便于用例排查。

4. **对照 `constant-errors` 逐条实现**
   - 做法：15 个 reject 用例按文件名分类（类型不对、路径不是常量、环、非法运算等），一条一条补规则。
   - 作用：这是最省力的反向实现方式——测试即规格。

**验收**：`make test FILTER=official:semantic:constants-and-paths,official:semantic:constant-errors STAGE=semantic` 全绿。
**常见坑**：`-2147483648i32` 要合法（先取负再判断）；溢出属于 UB，不用实现；括号和路径嵌套别漏。

---

## P5 语义类型基础设施

**为什么做**：之后每个表达式节点都要挂类型，类型判等、布局计算、错误打印到处都在用；这里没搭好，P6 会写得很痛苦。

1. **定义 `TypeKind`**
   - 做法：i32/u32/isize/usize/bool/unit/never/ref(mut, inner)/array(elem, len)/struct(id)/Box/Vec。
   - 作用：P6 的所有规则都基于这些种类；`never` 先定义好，P7 立即要用。

2. **类型 interning**
   - 做法：`TypeArena` 用 `map<key, TypeId>` 去重；比较类型就是比较 id。
   - 作用：类型判等 O(1)、复制便宜；数据流/后续阶段大量使用。key 要包含引用可变性、数组长度等所有区分信息。

3. **TypeSyntax → Type**
   - 做法：解析路径到 struct/内建类型；数组长度调用 P4；引用保留 mut。
   - 作用：P3 的签名解析、P6 的 `as`、P7 的 let 标注都靠它。

4. **结构体布局**
   - 做法：按字段声明顺序计算 offset/align/size（参考布局：字段按声明顺序、对齐取最大、整体向上取整）。
   - 作用：IR/CodeGen 直接使用；现在算好可以避免后期重复实现。

5. **递归布局检测**
   - 做法：值包含自身的结构体报错；经过 `Box`/`Vec` 的递归合法。
   - 作用：`recursive-layout` 用例；也决定后面容器语义的合法性。

6. **类型打印**
   - 做法：`std::string typeToString(TypeId)`。
   - 作用：错误信息可读性直接影响你的调试速度；Code Review 也会看诊断质量。

**验收**：`recursive-layout` accept 例可解析；随便构造两个类型错误能看到可读的类型名。
**常见坑**：RV32 上 `isize/usize` 是 4 字节、`bool` 1 字节；`Box`/`Vec` 是内建泛型类型而不是用户 struct；引用类型判等要区分 `&T` 与 `&mut T`。

---

## P6 表达式类型检查

**为什么做**：语义分析的核心工作量，绝大多数 reject 用例在这里产生；它也为 P7-P11 提供每个表达式的类型。

1. **确定 `infer`/`check` 接口**
   - 做法：`TypeId infer(Expr*, TypeId expected = null)`；在 let 标注、函数实参、结构体字段、数组元素处传 expected。
   - 作用：整数字面量选型需要 expected；规范保证 expected 不需要穿透二元/一元/借用（那是 UB），实现可以很简单。

2. **字面量选型**
   - 做法：后缀 → expected → `i32` 兜底；越界不处理（UB）。
   - 作用：`casts-and-literals` 的基础。

3. **一元/二元/比较/逻辑/移位**
   - 做法：写一张按 TypeKind 查的规则表；比较出 `bool`；`&&`/`||` 要求 bool；有无符号区分移位/比较。
   - 作用：`integer-arithmetic`、`shifts`、`boolean-and-short-circuit` 的主体。

4. **`as` 强转**
   - 做法：按 `casts-and-literals` 的接受/拒绝枚举允许的组合。
   - 作用：明确边界，别凭 Rust 印象放行。

5. **place 与赋值/复合赋值**
   - 做法：写一个 `isPlace(expr)`（变量、字段、索引、解引用、`*Box`）；赋值目标必须是可变 place；复合赋值展开时保证左值只求值一次（记在 AST 上）。
   - 作用：`compound-assignment`、`evaluation-order-and-temporaries`；也是 P10 可变性检查的基础。

6. **结构体/数组/索引/调用（与 P8/P9 交叉）**
   - 做法：这部分可以先用桩（返回 unknown）跑通 A 组，再在 P8/P9 补全。
   - 作用：控制实现节奏，先拿分简单的。

7. **把类型缓存在 AST 节点上**
   - 做法：每个表达式节点加一个 `TypeId` 字段，infer 时写入。
   - 作用：P7/P8/IR 阶段直接读取，避免重复推导和遍历时的重复工作。

**验收**：`integer-arithmetic`、`shifts`、`casts-and-literals`、`compound-assignment`、`evaluation-order-and-temporaries` 全绿。
**常见坑**：期望类型只在列出的位置传播；比较结果一定是 bool；不要在类型错误时抛异常/崩溃（必须优雅 exit 1）。

---

## P7 语句与控制流

**为什么做**：块/if/loop 决定类型如何统一，`Never` 类型和可达性又会影响后续检查；`blocks-if-and-never`、`loops-and-jumps`、`unreachable-checks` 都在这。

1. **block 类型**
   - 做法：有尾表达式取尾类型；没有则为 `()`。
   - 作用：函数体、if 分支、loop 体的基础。

2. **let**
   - 做法：有标注时先解析标注类型再用它 check 初值；无标注时用初值的推导类型。
   - 作用：局部类型推断的主要入口。

3. **if/else 统一**
   - 做法：两个分支同型取该型；一边 `Never` 取另一边；`if` 无 else 时 then 必须是 `()`；引用相关合并留到 P10。
   - 作用：`blocks-if-and-never` 的核心规则。

4. **循环与跳转**
   - 做法：`while` 条件必须 bool、整体 `()`；`loop` 无 break 为 `Never`，有 break 时统一 break 值；`while` 里 break 不能带值；`return` 检查返回类型。
   - 作用：`loops-and-jumps` 8 例。

5. **不可达代码仍要检查**
   - 做法：`return/break/continue/loop{}` 之后的代码标记不可达，但名字/类型/place 检查照常执行。
   - 作用：规范明确要求；`unreachable-checks` 的 accept/reject 会考这一点。

**验收**：`boolean-and-short-circuit`、`blocks-if-and-never`、`loops-and-jumps`、`unreachable-checks` 全绿。
**常见坑**：`loop` 带 break 值；`if` 两个分支都是 `Never` 时结果也是 `Never`；`return` 后不要因为“不可达”就跳过检查。

---

## P8 结构体与容器基础

**为什么做**：结构体、数组、`Box`/`Vec` 是测试用例的主要数据类型；它们的构造/索引/字段规则是后面的基础。

1. **结构体字面量与字段**
   - 做法：字段齐全、无重复、无多余、类型匹配；字段访问返回字段类型。
   - 作用：`structs-and-fields`。

2. **数组字面量**
   - 做法：列表形式逐元素检查同型；重复形式 `[e; N]` 中 N 是常量：**N>1 要求元素 Copy，N==1 不要求**。
   - 作用：`arrays` 的显式规则，也是 Copy 分类第一次真正用上。

3. **索引**
   - 做法：基类型只能是数组/Vec；下标必须是 `usize`；结果是元素类型的 place，可变性跟随基表达式。
   - 作用：`vec-index-mutability`、`arrays`。

4. **内建构造**
   - 做法：`Box::<T>::new`、`Vec::<T>::new` 必须有显式具体类型实参（测试保证，缺失是 UB）；返回内建容器类型。
   - 作用：所有容器用例的入口。

5. **内建方法与成员表**
   - 做法：把 `heap`/`builtin` 页的签名整理成代码里一张表：`len/is_empty/new/push/remove` 等，供 P9 点调用查询。
   - 作用：方法解析的数据来源；集中管理便于和规范核对。

6. **嵌套容器**
   - 做法：`Vec<Vec<i32>>`、`Box<[i32;4]>` 等递归解析即可，不需要特判。
   - 作用：`nested-containers`。

**验收**：`structs-and-fields`、`arrays`、`nested-containers`、`expected-types` 全绿。
**常见坑**：N==1 的重复字面量不要求 Copy；数组下标不是 usize 要拒；`Box`/`Vec` 构造的泛型实参解析路径。

---

## P9 方法与派生

**为什么做**：方法调用解析顺序和 derive 约束是最容易做错、也最容易在 Code Review 被问的点。

1. **impl 收集与合法性**
   - 做法：按目标类型挂方法表；重复方法名报错；非法 impl 目标（如内建类型、未定义类型）报错。
   - 作用：`methods-and-self`、`invalid-impls-and-generics`。

2. **self 三种形式**
   - 做法：`self`、`&self`、`&mut self`；调用时接收者类型要匹配（含自动引用/解引用）。
   - 作用：方法调用的核心；P10 的可变性检查也依赖 receiver 形式。

3. **点调用解析**
   - 做法：按 `接收者类型 → 自动取引用/解引用` 顺序查固有方法和内建方法；**派生生成的方法不参与点查找**（用户自定义 `clone` 覆盖点调用）。
   - 作用：`trait-dispatch-and-reference-equality`；多候选歧义是 UB，不必处理。

4. **关联函数路径调用**
   - 做法：`Type::f(...)` 直接在类型的方法/关联函数表里查。
   - 作用：`Outer::clone(&a)` 这类调用。

5. **derive 约束**
   - 做法：`Copy` 要求所有字段 Copy（Vec/Box 字段直接不合格）；`Clone` 要求字段 Clone；`PartialEq` 要求字段 PartialEq；`Eq` 要求先有 `PartialEq`。容器按其元素类型递归判断。
   - 作用：`copy-clone-and-equality`、`recursive-traits` 的拒绝规则。

6. **内建 I/O 签名**
   - 做法：`get_i32() -> i32`、`print_i32(i32)`、`println_i32(i32)`。
   - 作用：所有运行时用例必用；IR 阶段直接映射到 runtime 符号。

**验收**：`methods-and-self`、`builtin-io`、`trait-dispatch-and-reference-equality`、`invalid-impls-and-generics` 全绿。
**常见坑**：生成 clone 与用户 clone 的优先级；`#[derive(Copy)]` 遇到 Vec 字段必须拒；`Eq` 不能单独出现。

---

## P10 引用、可变性与 Copy 分类（无借用检查）

**为什么做**：规范明确**不要求**所有权/借用/lifetime 检查，但 place 可变性是编译错误（包括不可达代码）；Copy 分类供数组重复规则和后续 codegen 使用。做多了（写借用检查器）纯属浪费时间。

1. **`is_copy(Type)`**
   - 做法：整数/布尔/unit/引用/数组（元素 Copy）/派生 Copy 且字段全 Copy 的 struct 为真；Box/Vec 恒假。
   - 作用：数组重复规则；IR/CodeGen 选择按值拷贝还是移动。

2. **place 可变性检查**
   - 做法：赋值/复合赋值目标、`&mut` 借用目标、`&mut self` 接收者必须是可变 place；变量可变性、字段可变性、索引可变性逐层判断；不可达代码里也检查。
   - 作用：`box-and-moves`、`references-and-mutability`、`vec-index-mutability` 的 reject 主体。

3. **引用与自动调整**
   - 做法：点调用时的自动取引用/解引用；`*p` 是 place；`&`/`&mut` 的类型。
   - 作用：P9 方法查找的补充。

4. **强转与 LUB**
   - 做法：`&mut T → &T` 在实参、标注、分支合并处允许；分支合并的引用公共类型按 `reference-lub` 用例推导；“无公共目标”是 UB。
   - 作用：`reference-coercions`、`reference-lub`。

5. **Box 解引用与 Vec 索引的可变访问传播**
   - 做法：按 `heap` 页的例子逐条实现：owned Box 解引用不算借用，`&Box<T>` 不能提供可变访问；Vec 索引会隐式借用容器，`mut` 传播到最深一层。
   - 作用：`vec-index-mutability` 22 例的关键规则。

6. **明确不做**
   - 做法：移动后使用、借用冲突、lifetime 检查全部不做（UB，测试不会出现）。
   - 作用：把时间留给真正的编译错误。

**验收**：`box-and-moves`、`references-and-mutability`、`scalar-reference-operators`、`reference-coercions`、`reference-lub`、`lifetimes-and-use`、`copy-clone-and-equality`、`runtime-value-merges`、`loop-state-merges`、`live-state-calls` 全绿。
**常见坑**：不要实现借用检查器；不可变所有者通过 `&mut` 字段仍可改（Box 规则）；Vec 索引的可变传播别只看表层变量。

---

## P11 容器深层语义

**为什么做**：容器方法、递归 derive、聚合传参是容器用例的最后一块；也是 IR/CodeGen 时最容易出运行时 bug 的地方。

1. **Vec 方法全集**
   - 做法：`new/len/is_empty/push/remove/index/clone/eq` 的类型检查；“直接移出非 Copy 元素”是 UB（不用管），用 `remove` 才是合法途径。
   - 作用：`vec-operations`、`vec-index-mutability`。

2. **Box 语义**
   - 做法：deref（owned 移出非 Copy / Copy 拷贝）、clone、eq、作为值传递。
   - 作用：`box-and-moves`、`copy-clone-and-equality`。

3. **递归 derive**
   - 做法：`Vec<Self>` 字段的 struct 可以 derive Clone/PartialEq；不能 derive Copy。
   - 作用：`recursive-traits`。

4. **聚合传参/返回/引用字段**
   - 做法：结构体值参数、返回值、字段里的引用，按引用规则检查。
   - 作用：`aggregate-arguments-and-reference-fields`。

5. **零大小数据**
   - 做法：空结构体、零长数组、零大小字段可直接拒绝（UB，不在测试范围）。
   - 作用：省掉一批边界处理。

**验收**：`vec-operations`、`vec-index-mutability`、`recursive-traits`、`aggregate-arguments-and-reference-fields` 全绿。
**常见坑**：递归 derive 的终止条件（容器按元素递归）；`Eq` 对容器的要求；引用字段的可变性。

---

## P12 收尾与验收

**为什么做**：把零散规则收敛成完整通过，并准备 Code Review；Review 失败等于该阶段未完成。

1. **扫尾剩余 reject 用例**
   - 做法：每次全量跑，按目录归纳失败原因（同一规则一起修）；优先修“一个规则挂很多用例”的点。
   - 作用：用最短时间提高通过率。

2. **对照 UB 清单自查**
   - 做法：核对 `undefined-behavior` 页，确认没有实现多余的检查（ownership/lifetime/import），也没有漏掉必查项（名字/类型/place 可变性/保护名）。
   - 作用：防止过度实现或漏实现。

3. **攻关 `comprehensive-*`**
   - 做法：5 个大用例逐个用 `--dump-ast` + 错误信息定位；它们通常是前面规则的组合，别单独打补丁。
   - 作用：验证整体一致性。

4. **全量回归**
   - 做法：`make test FILTER=official:semantic STAGE=semantic`，目标是 0 failed。
   - 作用：阶段验收硬指标。

5. **补 `tests/custom/` 用例**
   - 做法：每实现一个规则补一个 acc/rej 小用例（命名 `acc-*`/`rej-*`，带 `manifest.json`）。
   - 作用：防回归；Code Review 时展示你对规则的理解。

6. **写 `docs/DESIGN.md`**
   - 做法：pipeline 图、AST 结构、符号表/类型系统、Copy 规则、已知简化与原因。
   - 作用：Code Review 的主讲材料。

7. **Review 演练与打 tag**
   - 做法：能现场演示 dump、任意 reject 的错误信息；能回答“这个目录对应哪条规则、实现在哪”。打 `git tag ast-ok`。
   - 作用：把 Review 从“被问倒”变成“主动展示”。

**验收**：`make test FILTER=official:semantic STAGE=semantic` 全绿；Review 通过。

---

**依赖顺序**：P0 → P1 → P2 → P3 → P4 → P5 → P6/P7/P8 可交错 → P9 → P10 → P11 → P12。
**卡壳时的求助方式**：说清“在做哪个 P、哪条规则、测试用例文件名、你的预期和实际输出”，再发我或助教。
