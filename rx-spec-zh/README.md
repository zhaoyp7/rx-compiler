# Rx 编译器规范（中文翻译）

> **性质**：本目录是官方英文规范的非官方中文翻译，仅供个人学习阅读。
> **权威性**：语义以官方英文原文为准；中译与原文不一致时以原文为准，发现歧义请向助教确认。
> **翻译基准**：官方仓库 commit `42210fe`（2026-09-25，"fix: correct unit bindings, array initializers, and clone lookup wording"）。
> **原文位置**：在线阅读 <https://acmclasscourse-2025.github.io/rx-compiler-specification/>；源码仓库 <https://github.com/ACMClassCourse-2025/rx-compiler-specification>。
> **编号与目录规则**：每章一个子文件夹 `NN-<名称>/`；章节目录页为 `NN.00-<名称>.md`，子章节为 `NN.MM-<名称>.md`，编号与官方站点侧边栏一致（如 `03.01`、`11.10`）。Introduction 官方无编号，记为 `00`；`open-decisions` 未收入官方目录，记为 `18`；`README.md`、`SUMMARY.md` 为索引文件，不编号。
> 代码块、标识符、链接目标均保持原文不变；仅叙述性文字翻译为简体中文。

## 推荐阅读顺序

1. [语言范围与测试保证](01-undefined-behavior/01.00-undefined-behavior.md)（`undefined-behavior`）—— 先明确哪些必须实现、哪些属于未定义行为（UB）
2. [解析器约定](04-grammar/04.00-grammar.md) —— 可丢弃语法（`use`/lifetime）、上下文标点
3. [词法结构](03-lexical-structure/03.00-lexical-structure.md) → [词法单元](03-lexical-structure/03.06-tokens.md) → [关键字](03-lexical-structure/03.02-keywords.md) → [标识符](03-lexical-structure/03.03-identifiers.md) → [注释](03-lexical-structure/03.04-comments.md) → [空白](03-lexical-structure/03.05-whitespace.md)
4. [项](07-items/07.00-items.md)、[类型](10-types/10.00-types.md)、[表达式](11-expressions/11.00-expressions.md)、[语句与绑定](09-statements/09.00-statements.md)、[文法速查](17-grammar-summary/17.00-grammar-summary.md)
5. 用到再查：[名称与作用域](06-names/06.00-names.md)、[常量上下文](12-const_eval/12.00-const_eval.md)、[Box 与 Vec](14-heap/14.00-heap.md)、[内建 trait](08-traits-and-attributes/08.01-builtin-traits.md)、[引用与存储期](13-references/13.00-references.md)、[后端](16-backend/16.00-backend.md)

## 文件索引

| 章 | 文件夹 | 文件 |
|---|---|---|
| 00 | `00-introduction/` | `00.00-introduction.md` |
| 01 | `01-undefined-behavior/` | `01.00-undefined-behavior.md` |
| 02 | `02-notation/` | `02.00-notation.md` |
| 03 | `03-lexical-structure/` | `03.00-lexical-structure.md`、`03.01-input-format.md`、`03.02-keywords.md`、`03.03-identifiers.md`、`03.04-comments.md`、`03.05-whitespace.md`、`03.06-tokens.md` |
| 04 | `04-grammar/` | `04.00-grammar.md` |
| 05 | `05-crates-and-source-files/` | `05.00-crates-and-source-files.md` |
| 06 | `06-names/` | `06.00-names.md`、`06.01-paths.md` |
| 07 | `07-items/` | `07.00-items.md`、`07.01-use-declarations.md` … `07.07-generics.md` |
| 08 | `08-traits-and-attributes/` | `08.00-traits-and-attributes.md`、`08.01-builtin-traits.md` |
| 09 | `09-statements/` | `09.00-statements.md` |
| 10 | `10-types/` | `10.00-types.md`、`10.01-array.md` … `10.05-never.md` |
| 11 | `11-expressions/` | `11.00-expressions.md`、`11.01-literal-expr.md` … `11.14-return-expr.md` |
| 12 | `12-const_eval/` | `12.00-const_eval.md` |
| 13 | `13-references/` | `13.00-references.md` |
| 14 | `14-heap/` | `14.00-heap.md` |
| 15 | `15-builtin/` | `15.00-builtin.md` |
| 16 | `16-backend/` | `16.00-backend.md` |
| 17 | `17-grammar-summary/` | `17.00-grammar-summary.md` |
| 18 | `18-open-decisions/` | `18.00-open-decisions.md` |
| — | 根目录 | `README.md`、`SUMMARY.md` |

## 术语对照（摘要）

有效程序 valid program、编译错误 compile error、未定义行为 undefined behavior、词法单元 token、项 item、位置 place、强制转换 coercion、类型推断 type inference、期望类型 expected type、最小上界 LUB、派生 derive、接收者 receiver、关联函数 associated function、调用约定 calling convention。
