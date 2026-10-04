# 阶段一（AST + 语义）学习手册

> 配套 `docs/PHASE1.md` 使用：P 编号一一对应。每个阶段回答四个问题：为什么学、学什么/在哪读、怎么练、学到什么程度。
> 资料优先用中译版 `rx-spec-zh/`（目录已带章号）；Rust 语义细节用官方英文原站交叉验证。
> 先记住一条：**有些东西不需要学**，见文末“不用学清单”。

---

## 前置知识（P0 之前必须补）

**为什么**：你的目标是“读懂规范 + 写得出解析器的使用者”，不是从零造语言理论。下面这些是理解后续所有内容的最小背景。

**学什么 / 读哪里**
- 编译流水线（词法→语法→AST→语义→IR→代码生成，每步输入输出）：龙书第 1 章；[compiler-101](https://github.com/ACMClassCourses/compiler-101/tree/main) 导论。
- Rx 的语言范围与测试保证（先读！）：`rx-spec-zh/01-undefined-behavior/01.00-undefined-behavior.md`。
- 可丢弃语法（`use`/lifetime）：`rx-spec-zh/04-grammar/04.00-grammar.md`。
- Rust 基础语法（变量/`mut`/函数/结构体/`impl`/数组/引用）：[Rust Book](https://doc.rust-lang.org/book/) 1-6 章 + 10 章；或者边读 Rx 规范边查。
- C++：类与继承、`unique_ptr`、`std::variant/optional`、`unordered_map`、头文件组织：cppreference / 《C++ Primer》对应章节。
- 构建：CMake 官方 Tutorial 的前两章。
- 测试契约：`README-ZH.md`、`scripts/test.py`、`tests/official/manifest.schema.json`。

**动手练**
- 在官方 [Rust Playground](https://play.rust-lang.org/) 里手写 10 个 Rx 风格的片段，看哪些能编译、哪些报错。
- 读 `tests/official/semantic/entry/` 全部 5 个文件，说出每个 accept/reject 的原因。

**学到什么程度**：能回答“reject 用例要求编译器退出码是什么”“为什么 `use` 和 lifetime 可以先不管”“`Box`/`Vec` 是谁提供的”。

---

## P0 工程骨架

**为什么学**：你需要理解“测试运行器如何调用你的编译器”和“ANTLR 生成代码如何被使用”，否则 P0 只是抄命令，出错也不知道错在哪。

**学什么 / 读哪里**
- ANTLR4 基本概念：文法、词法/语法分析、parse tree、visitor/listener：`rx-spec-zh/04-grammar/04.00-grammar.md` + ANTLR 官方文档的 Getting Started。
- ANTLR C++ runtime API：`ANTLRInputStream`、`CommonTokenStream`、`Lexer`/`Parser` 构造、`parser.crate()`、`getNumberOfSyntaxErrors`：生成后的 `src/generated/RxParser.h` 就是最好的文档。
- 错误监听器：`antlr4::BaseErrorListener` 的 `syntaxError` 回调；理解 ANTLR 默认“报错后继续恢复”的行为。
- CMake 最小用法：一个可执行目标 + include + link。
- 测试运行器契约：`scripts/test.py` 里命令如何展开、退出码如何判定。

**动手练**
- 写一个只打印全部 token 的小程序。
- 故意写一个语法错误的 `.rx`，确认你的 `syntaxError` 回调被调用且最终 exit 1。
- 遍历 236 个 semantic 源码统计语法错误，目标为 0。

**学到什么程度**：不看笔记能说出从 `.rx` 到 `build/rxc` 的完整命令链；能解释为什么必须改名生成 RxLexer/RxParser。

---

## P1 AST 定义

**为什么学**：AST 是后面所有阶段的接口。你要先理解“语法树 ≠ AST”，才能决定哪些节点该合并、哪些字段该留。

**学什么 / 读哪里**
- AST vs parse tree、为什么要做语法糖归一化：任意编译器教材的“抽象语法树”一节；结合 `rx-spec-zh/04-grammar/04.00-grammar.md` 里影子规则的说明。
- Rx 的 item/type/expression 结构：`rx-spec-zh/07-items/`、`rx-spec-zh/10-types/`、`rx-spec-zh/11-expressions/`（你在前一轮读过，这里把它当节点清单用）。
- C++ 树建模：继承 + `unique_ptr`，或 `std::variant`；源码位置结构。
- 枚举字段的取舍：哪些信息 P6 需要（如左值、运算符种类）。

**动手练**
- 拿一个 30 行的 `.rx`（比如 `tests/official/semantic/comprehensive-quicksort/` 的文件）手画 AST。
- 从文法里抄出所有表达式产生式，逐个映射到你的节点名，做成一张对照表放进 `docs/DESIGN.md`。

**学到什么程度**：给你一个表达式，能立刻说出它在 AST 里是什么节点、有哪些字段；能解释 `&&x` 为什么不是逻辑与。

---

## P2 AST 构建

**为什么学**：这一阶段要跟 ANTLR 生成的 context 类打交道，核心技能是“按规则形状写 visitor”和“剥离影子规则”。

**学什么 / 读哪里**
- `RxParserBaseVisitor` 与 `std::any` 的用法：生成的 `src/generated/RxParserBaseVisitor.h`；官方 ANTLR 的 visitor 文档。
- 左递归消除后的规则形状：`a (op a)*` 怎么折成左结合二元表达式。
- 可丢弃语法的处理位置：`rx-spec-zh/04-grammar/04.00-grammar.md` 的 “Syntax that may be discarded after parsing”。
- 块的尾表达式规则：`rx-spec-zh/11-expressions/11.03-block-expr.md`。

**动手练**
- 只实现“函数 + 块 + 整数加法/赋值”四种节点，跑通一个最小 dump。
- 打印一个含 `if/loop/method call` 文件的 AST，和文法逐条对照。

**学到什么程度**：遇到任何 parse tree 形状，能判断它是“左折叠、后缀循环、还是普通备选”，并写出对应的 visit 代码；语法错误时能确保不建树。

---

## P3 符号与名字解析

**为什么学**：类型检查的第一步是“这个名字是什么”。没有符号表和两遍扫描，前向引用和递归函数都会挂。

**学什么 / 读哪里**
- 作用域、遮蔽、前向引用、两遍扫描：编译原理教材的“符号表/语义分析”一节（龙书第 5 章）。
- Rx 的名字规则：`rx-spec-zh/05-crates-and-source-files/`、`rx-spec-zh/06-names/`、`rx-spec-zh/07-items/07.00-items.md`。
- 保护名：`rx-spec-zh/06-names/06.00-names.md`（`undefined-behavior` 页有对应 UB 说明）。
- `main` 入口限制：`rx-spec-zh/15-builtin/15.00-builtin.md`。

**动手练**
- 把 `namespace-errors` 的 16 个 reject 按“重复定义/未定义/遮蔽/保护名”分类，跟着文件名写规则。
- 手推 `let x = 1; { let x = 2; }` 的作用域变化。

**学到什么程度**：能解释“为什么必须先收集所有顶层 item 再检查函数体”；能说出遮蔽和重复定义的区别。

---

## P4 常量求值

**为什么学**：数组长度和重复次数要在编译期有确切值；你需要一个最小解释器。

**学什么 / 读哪里**
- 树遍历求值、memo、DFS 环检测：编译原理教材“常量折叠/常量传播”的概念部分（只学求值，不做优化）。
- Rx 常量上下文范围：`rx-spec-zh/12-const_eval/12.00-const_eval.md`。
- 数组长度规则：`rx-spec-zh/10-types/10.01-array.md`、`rx-spec-zh/11-expressions/11.07-array-expr.md`。

**动手练**
- 手工计算 `const N = 2 + 3; [i32; N * 2]` 的长度。
- 找出 `constant-errors` 里环引用用例，说明它为什么必须报错。

**学到什么程度**：能说出哪些位置允许常量表达式（规范限制），以及为什么“通用常量求值”不在范围内。

---

## P5 语义类型基础设施

**为什么学**：类型是语义阶段的“货币”，判等、布局、打印都会被高频使用；interning 是性能与简洁的关键。

**学什么 / 读哪里**
- 类型表示与类型驻留（interning/hash-consing）：编译原理教材“类型系统”一节；C++ `unordered_map` 做 key→id。
- 内存布局：`rx-spec-zh/16-backend/16.00-backend.md` 的 Data layout 表（字段顺序、对齐、数组、bool/引用宽度）。
- 递归类型：`rx-spec-zh/07-items/07.03-structs.md`（经 `Box`/`Vec` 的递归为什么合法）。

**动手练**
- 手算这些类型在 RV32 上的 size/align：`i32`、`bool`、`[u32; 3]`、`struct { bool; i32 }`、`&mut [i32;2]`。
- 找出 `recursive-layout` 里合法与非法各一个例子并说明原因。

**学到什么程度**：能解释“为什么类型判等可以用 id 比较”，以及 `&T` 与 `&mut T` 为什么必须是不同 key。

---

## P6 表达式类型检查

**为什么学**：这是语义阶段的主战场；你将学会类型检查器最常用的“推导 + 期望类型”模式。

**学什么 / 读哪里**
- 类型推导/检查：编译原理教材的类型检查章节；Rust Reference 的 expression 部分。
- Rx 运算符与类型规则：`rx-spec-zh/11-expressions/11.04-operator-expr.md`（最核心的一篇）以及相关子章节。
- `as` 转换：`rx-spec-zh/11-expressions/11.04-operator-expr.md` 的 cast 部分。
- 整数选型与期望类型：`rx-spec-zh/10-types/10.00-types.md`、`rx-spec-zh/11-expressions/11.01-literal-expr.md`。

**动手练**
- 给 20 个手写表达式人工标注类型，再和你的实现对照。
- 对 `casts-and-literals` 的每个用例，先写下“允许/拒绝 + 理由”，再跑测试。

**学到什么程度**：能解释什么时候需要 expected type、为什么规范保证它不穿透二元/一元/借用；能给任意 reject 用例指出违反了哪条规则。

---

## P7 语句与控制流

**为什么学**：控制流决定了类型如何合并，`Never` 是理解“发散表达式”的钥匙；不可达代码的检查策略也是规范考点。

**学什么 / 读哪里**
- 语句/块/尾表达式的值：`rx-spec-zh/09-statements/09.00-statements.md`、`rx-spec-zh/11-expressions/11.03-block-expr.md`。
- if/loop/while/break/return：`rx-spec-zh/11-expressions/11.13-if-expr.md`、`11.12-loop-expr.md`、`11.14-return-expr.md`。
- Never 类型与不可达代码：`rx-spec-zh/10-types/10.05-never.md`。

**动手练**
- 给出 10 个函数片段，人工写出每个块/分支/循环的类型。
- 对一段“`return` 后还有类型错误”的代码，判断规范要求 accept 还是 reject。

**学到什么程度**：能说出 `loop`/`while`/`if` 各自的类型规则和 break 的约束；能解释为什么不可达代码仍要检查。

---

## P8 结构体与容器基础

**为什么学**：结构体/数组/容器是测试数据的主要形态；Copy 规则也在这里首次真正参与语义。

**学什么 / 读哪里**
- 结构体字面量/字段：`rx-spec-zh/07-items/07.03-structs.md`、`11-expressions/11.08-struct-expr.md`、`11.11-field-expr.md`。
- 数组与索引：`rx-spec-zh/10-types/10.01-array.md`、`11-expressions/11.07-array-expr.md`。
- 容器类型与构造：`rx-spec-zh/10-types/10.03-heap.md`。
- 方法签名总表：`rx-spec-zh/14-heap/14.00-heap.md`。

**动手练**
- 手推 `[e; 1]` 与 `[e; 2]` 在 `e` 非 Copy 时的区别。
- 把 `heap` 页的容器方法整理成一张“方法名 → 接收者形式 → 参数 → 返回类型”的表。

**学到什么程度**：能解释 `Box::<i32>::new` 和 `Box::new` 的区别（后者是 UB）；能说出索引下标的类型要求。

---

## P9 方法与派生

**为什么学**：方法查找顺序、derive 约束是语义里最容易想当然出错的地方，也是 Code Review 高频问题。

**学什么 / 读哪里**
- 方法调用与接收者：`rx-spec-zh/11-expressions/11.10-method-call-expr.md`。
- 关联函数与 impl：`rx-spec-zh/07-items/07.05-implementations.md`、`07.06-associated-items.md`。
- 内建 trait 能力表：`rx-spec-zh/08-traits-and-attributes/08.01-builtin-traits.md`。
- 派生与属性：`rx-spec-zh/08-traits-and-attributes/08.00-traits-and-attributes.md`。

**动手练**
- 对 5 个 `x.m()` 调用，手写类型调整过程（取引用/解引用几层）和命中的方法。
- 给 `copy-clone-and-equality` 的 reject 用例各写一句“违反了哪条 derive 约束”。

**学到什么程度**：能说清“为什么派生 clone 不参与点查找”，以及 `Eq` 对 `PartialEq` 的依赖。

---

## P10 引用、可变性与 Copy 分类（不学借用检查）

**为什么学**：规范只要求 place 可变性检查；**不要学借用检查器/NLL/生命周期求解**，那会浪费大量时间且不做要求。

**学什么 / 读哪里**
- place 表达式与可变性：Rust Reference 的 “Assignments”“Borrowed pointers”概念；`rx-spec-zh/11-expressions/11.04-operator-expr.md` 的 assignment destinations。
- 引用类型与强转：`rx-spec-zh/10-types/10.02-pointer.md`、`rx-spec-zh/13-references/13.00-references.md`。
- Box/Vec 的访问与可变传播：`rx-spec-zh/14-heap/14.00-heap.md`（里面的正反例就是规则本身）。
- 能力表：`rx-spec-zh/08-traits-and-attributes/08.01-builtin-traits.md`（Copy 分类依据）。

**动手练**
- 把 `vec-index-mutability` 的 acc/rej 用例抄到纸上，标出“可变性从哪个变量传播到哪一层”。
- 给 `references-and-mutability` 的每个 reject 指出具体违规的 place。

**学到什么程度**：能解释“为什么 `&Box<T>` 不能给可变访问，而 `Box<&mut T>` 可以”；明确知道移动/借用检查不做。

---

## P11 容器深层语义

**为什么学**：容器的克隆、比较、递归类型是最后一块拼图，直接决定 IR/CodeGen 的行为正确性。

**学什么 / 读哪里**
- 容器操作与生命周期：`rx-spec-zh/14-heap/14.00-heap.md`。
- 容器类型规则：`rx-spec-zh/10-types/10.03-heap.md`。
- 内置 trait 对容器的递归要求：`rx-spec-zh/08-traits-and-attributes/08.01-builtin-traits.md`。

**动手练**
- 手工写出 `Vec<Node>` 的 `clone`/`==` 的递归条件（哪些能力必须存在）。
- 解释“为什么 `struct N { children: Vec<N> }` 可以 derive Clone 但不能 derive Copy”。

**学到什么程度**：能对任意嵌套容器说出 clone/eq 需要哪些元素能力，以及运行时要做几层分配。

---

## P12 收尾与验收

**为什么学**：测试定位方法、UB 清单核对、Code Review 表达，是把“能跑”变成“拿到分”的关键。

**学什么 / 读哪里**
- 失败日志：`scripts/test.py` 的输出目录结构（`target/tests/run-*/NNNN/`）。
- UB 清单：`rx-spec-zh/01-undefined-behavior/01.00-undefined-behavior.md`（逐条核对）。
- 自定义用例格式：`tests/official/manifest.schema.json` + `tests/custom/`。
- 表达与文档：怎么讲清架构、规则与实现位置的对应关系。

**动手练**
- 建立一张表：每个 semantic 目录 → 对应规则 → 你的实现文件/函数。
- 模拟 Review：随机抽 3 个 reject 用例，现场解释错误原因和触发检查。

**学到什么程度**：任何失败用例能定位到“规则 → 代码 → 测试”三者的连接点；Review 时主动讲解而不是被动挨问。

---

## 不用学清单（规范明确排除，学了也没分）

- 借用检查器 / NLL / 生命周期求解 / 生命周期名称解析。
- import 解析、模块加载、别名与冲突检查（`use` 解析后丢弃）。
- 函数作为值（函数指针）、用户 trait / trait 对象 / trait 约束。
- 枚举、模式匹配、元组、类型参数（用户泛型）、const 泛型。
- 通用常量求值、整数溢出检查、panic/栈展开。
- 跨类型相等、零大小数据的可观察使用（都是 UB，测试不出现）。
- 借用/所有权错误诊断（不做检查）。

**学习节奏建议**：P0-P2 先动手感受流水线；P3-P5 是理论核心；P6-P9 规则最多、最依赖测试用例驱动；P10 只学 place 可变性与引用规则，**不要碰借用检查器**。
