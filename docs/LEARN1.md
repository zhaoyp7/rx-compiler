# 阶段一（AST + 语义）学习清单

> 配套 `docs/PHASE1.md` 使用：每个学习小节对应那里的相同编号阶段。
> 方法统一为三步：① 读资料 → ② 拿测试用例手工推演一遍 → ③ 再写代码。

## 前置知识（P0 之前必须补）

| 缺口 | 学什么 | 资料 |
|---|---|---|
| 编译器是什么 | 编译流水线：词法→语法→AST→语义→IR→代码生成；每步的输入输出 | 龙书第 1 章；[compiler-101](https://github.com/ACMClassCourses/compiler-101/tree/main) 导论部分 |
| Rust 语言 | 变量与 `mut`、函数、结构体、`impl`、数组、引用 `&/&mut`、基本所有权概念 | [Rust Book](https://doc.rust-lang.org/book/) 第 1-6 章 + 第 10 章 |
| C++ | 类与继承、`unique_ptr`、`std::variant/optional`、头文件组织、`unordered_map` | cppreference；《C++ Primer》对应章节 |
| 构建工具 | CMake 最小用法：target、include、link 库 | CMake 官方 Tutorial |
| 测试契约 | `scripts/test.py`、`manifest.schema.json`、`{source}/{output}/{runtime}` 占位符 | 仓库内文件 |

**自测**：能读懂 `tests/official/semantic/entry/*.rx` 每一行为什么 accept/reject。

---

## P0 工程骨架

- **ANTLR4 基础**：文法、lexer/parser、parse tree、visitor/listener 概念；命令行生成 C++ 代码
- **ANTLR4 C++ runtime API**：`ANTLRInputStream`、`CommonTokenStream`、`Lexer/Parser` 构造、入口规则调用、`getNumberOfSyntaxErrors`
- **错误监听器**：`BaseErrorListener`、`syntaxError` 回调、ANTLR 错误恢复机制
- **C++ 集成**：编译 ANTLR 生成文件、链接 `libantlr4-runtime`
- **练习**：写一个程序打印 `.rx` 文件的所有 token；统计 236 个语义测试源码的语法错误数

## P1 AST 定义

- **AST 概念**：抽象语法树 vs 具体语法树（parse tree）的区别；为什么要做归一化
- **文法阅读**：`Lexer.g4`/`Parser.g4` 全部产生式，特别是 `condition*`/`statement*`/`closed*` 影子规则存在的目的
- **Rust 语法**：item/statement/expression/type 四类语法结构
- **C++ 建模**：继承体系、`unique_ptr` 所有权、`SourceSpan`/源码位置
- **练习**：手画一个 10 行 `.rx` 程序的 AST；从文法列出所有节点清单，确保无遗漏

## P2 AST 构建

- **ANTLR Visitor 模式**：`RxParserBaseVisitor`、`std::any`、`visitChild`、context 类的访问器
- **文法到 AST 的映射技巧**：左递归规则折叠成二元表达式、后缀循环、语法糖剥离
- **练习**：读生成的 `RxParser.h` 里 context 类字段，和文法产生式一一对应；给 3 个不同函数建树并 dump

## P3 符号与名字解析

- **符号表与作用域**：作用域链、遮蔽、两遍扫描、前向引用
- **Rust 名字解析**：路径语法（`self::`/`super::`/`crate::`/`Self`）、`use` 的 glob/alias/group、命名空间冲突
- **C++ 数据结构**：哈希表、字符串驻留
- **练习**：把 `namespace-errors` 的 16 个 reject 用例按错误类型分类；手工解析几个 accept 例的名字引用

## P4 常量求值

- **树遍历解释器**：递归求值、memoization、DFS 环检测
- **Rust 常量语义**：const item、数组长度必须是常量、常量表达式限制
- **练习**：手工算出 `constant-errors` 每个 reject 的原因；实现一个只有 int/bool/path 的求值器

## P5 语义类型基础设施

- **类型系统基础**：类型表示、结构等价 vs 名义等价、类型驻留（interning/hash-consing）
- **内存布局**：size/align/offset、结构体字段排布、数组布局、递归类型为什么无限大
- **类型打印**：错误信息回显类型
- **练习**：手算几个结构体（含数组/嵌套）的 size 和 offset；找出 `recursive-layout` 哪些合法哪些非法

## P6 表达式类型检查

- **类型推导/检查**：`infer` 与 `check` 双向检查、expected type、整数字面量默认类型
- **运算符类型规则**：算术/位/移位/比较/逻辑短路；有无符号区分
- **`as` 强转**：Rust 允许的转换组合
- **左值与可变性**：place expression、赋值/复合赋值的目标检查
- **练习**：拿 `integer-arithmetic`、`shifts`、`casts-and-literals` 的 accept/reject 逐个说明类型规则

## P7 语句与控制流

- **语句 vs 表达式**：块的值、尾表达式、unit 类型
- **控制流类型**：if/else 分支统一、循环类型、`break` 带值、`return`
- **Never 类型（`!`）**：发散表达式、类型统一里的特殊角色
- **可达性分析**：不可达代码判定
- **练习**：给 10 个控制流片段手工标注类型；实现 if/else 与 loop 的统一规则

## P8 结构体与容器基础

- **结构体字面量**：字段完整性、重复、类型匹配
- **数组语义**：列表字面量、重复字面量的 Copy 限制、长度常量
- **索引与 place**：数组/Vec 索引、下标类型 `usize`、结果可变性
- **内建容器**：`Box`/`Vec` 作为编译器预定义类型、`new/len` 签名
- **练习**：逐个解释 `arrays`、`structs-and-fields`、`nested-containers` 用例

## P9 方法与派生

- **方法解析**：`self`/`&self`/`&mut self`、自动引用/解引用、关联函数
- **impl 合法性**：重复方法、非法目标类型
- **derive 机制**：`Copy/Clone/PartialEq/Eq` 的语义与约束（字段传递、Vec 的特殊性）
- **方法优先级**：固有方法 vs 派生方法
- **练习**：手工模拟几个点调用的查找顺序；解释 `trait-dispatch-and-reference-equality` 里 clone 的行为

## P10 移动与所有权

- **所有权模型**：move 与 Copy、移动后使用、重新赋值
- **借用检查**：共享/可变借用冲突、借用生命周期（NLL 思想）
- **数据流分析**：CFG、transfer function、join、不动点迭代
- **引用规则**：`&mut T → &T` 强转、引用 LUB、生命周期参数基础
- **练习**：对 `loop-state-merges` 等 3 个 merge 用例手画 CFG 和状态表；实现一遍数据流

## P11 容器深层语义

- **递归类型与 derive 交互**：`Vec<Self>`、递归 Clone/PartialEq
- **容器完整方法语义**：push/index/len/clone/eq 的类型约束
- **聚合值传递**：结构体传参/返回、引用字段的移动与借用
- **练习**：解释 `recursive-traits`、`aggregate-arguments-and-reference-fields` 每个用例

## P12 收尾与验收

- **测试方法**：如何从失败日志定位（`target/tests/run-*`）、如何写自定义用例
- **文档与表达**：架构图、设计文档、Code Review 怎么讲
- **练习**：为每个测试目录写一句话“规则→实现位置”；准备现场演示 dump/reject

---

**学习节奏建议**：P0-P2 先做，动手感受流水线；P3-P5 是语义分析的理论核心；P6-P9 规则最多，靠测试用例驱动；P10 最难，数据流分析要单独花时间学。
