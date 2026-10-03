# 阶段一（AST + 语义）分阶段计划

> 对应第 4 周 AST 验收。按依赖顺序划分小阶段，不按时间划分。
> 总目标：`make test FILTER=official:semantic STAGE=semantic` 全绿（236 例 / 45 目录）。

## P0 工程骨架
- 备份 `config.mk` → `config.reference.mk`
- `tools/antlr-4.13.2-complete.jar` 就位
- 写 `scripts/gen_parser.sh`（改名生成 RxLexer/RxParser）
- 写 `CMakeLists.txt`，产出 `build/rxc`
- 写 `src/main.cpp` CLI：`--stage parse|semantic|ast|ir|codegen` + `-o`
- 写自定义 `ErrorListener`，确定退出码契约：0/1/其它
- 改 `config.mk`，跑通 `make test FILTER=official:semantic:entry STAGE=semantic` 调用链

**验收**：`cmake --build` 一把过；解析全部 236 个 semantic 源码零语法错误

---

## P1 AST 定义
- 定义 `SourceSpan`
- 定义 item 节点：Use/Fn/Struct/Const/Impl
- 定义 TypeSyntax 节点
- 定义 stmt 节点：Let/ExprStmt
- 定义 expr 节点全集（字面量、路径、调用、方法、字段、索引、一元/二元、赋值、强转、if/loop/while、break/continue/return、引用/解引用、块、结构体/数组字面量）
- 定义 ConstValue 节点

**验收**：节点覆盖文法所有产生式，无遗漏

---

## P2 AST 构建
- `ASTBuilder : RxParserBaseVisitor`：items + types
- 表达式下降（左折叠、后缀循环、影子规则归一化）
- 语句/块下降（含尾表达式）
- 语法错误时不建树
- `--stage ast` 打印器（缩进树）

**验收**：对 5 个覆盖不同语法的用例人工核对 dump 结果

---

## P3 符号与名字解析（目标 39 例）
- 全局符号表 + 第一遍 item 收集（函数签名/结构体/常量/impl）
- 作用域链、let 遮蔽
- 重复定义检查
- 路径解析：`self::`/`super::`/`crate::`/`Self`
- `use` 解析：glob/alias/group
- 内建符号注册：`rx::core`、`print_i32/println_i32/get_i32`、`Box/Vec`
- 保护名检查
- `main` 签名检查（唯一、无参、无泛型、返回 unit）

**验收**：`entry, names-and-shadowing, namespace-errors, protected-names` 全绿

---

## P4 常量求值（目标含在 P3 的 39 例内）
- 常量 AST 求值器（整数/布尔/路径/取负/括号）
- 支持 const 引用、memo、环检测
- 接数组长度与 `[e; N]` 重复次数
- 逐个实现 `constant-errors` 的 reject 规则

**验收**：`constants-and-paths, constant-errors` 全绿

---

## P5 语义类型基础设施
- 定义 `TypeKind`：i32/u32/isize/usize/bool/unit/never/ref/array/struct/Box/Vec
- 类型 interning 与类型相等
- TypeSyntax → Type 解析
- 结构体布局：offset/align/size
- 递归布局检测
- 类型打印（错误信息可读）

**验收**：`recursive-layout` accept 例可解析类型；错误信息能带类型

---

## P6 表达式类型检查
- `infer(expr, expected)` 骨架
- 整数字面量：默认 `i32`、后缀、expected 回填
- 一元/二元运算规则
- 比较、逻辑短路
- 移位规则
- `as` 强转合法组合
- 赋值/复合赋值的 place 与可变性检查
- 求值顺序相关规则（`a[i] += 1` 只求值一次）

**验收**：`integer-arithmetic, shifts, casts-and-literals, compound-assignment, evaluation-order-and-temporaries` 全绿

---

## P7 语句与控制流
- block 类型（尾表达式）
- let 有无类型标注两条路径
- if/else 分支类型统一
- if 无 else 要求 unit
- while 条件与类型
- loop/break 值统一
- return 类型检查
- `Never` 类型传播
- 不可达代码检查

**验收**：`boolean-and-short-circuit, blocks-if-and-never, loops-and-jumps, unreachable-checks` 全绿

---

## P8 结构体与容器基础
- 结构体字面量：字段齐全/无重复/无多余/类型匹配
- 字段访问
- 数组列表字面量
- 数组重复字面量 + Copy 规则（N>1 需 Copy，N==1 不需）
- 索引：数组/Vec + `usize` 下标
- `len()`
- 内建 `Box::<T>::new`、`Vec::<T>::new`
- 嵌套容器

**验收**：`structs-and-fields, arrays, nested-containers, expected-types` 全绿

---

## P9 方法与派生
- impl 收集与合法性校验
- self 参数三种形式
- 点调用解析（自动引用/解引用顺序）
- 关联函数路径调用
- `#[derive(Copy/Clone/PartialEq/Eq)]` 约束检查
- 派生方法 vs 固有方法优先级（派生 clone 不参与点查找）
- `builtin-io` 的函数签名

**验收**：`methods-and-self, builtin-io, trait-dispatch-and-reference-equality, invalid-impls-and-generics` 全绿

---

## P10 移动与所有权
- `is_copy(Type)` 判定
- 移动点：let/赋值/实参/结构体与数组字面量/return
- 移动后使用报错、重新赋值复活
- 借用检查：共享/可变借用冲突
- CFG 数据流：移动/借用状态合并（分支 + 循环不动点）
- 引用强转 `&mut T → &T`
- 引用 LUB
- lifetime 参数基础检查
- `box-and-moves` 起步

**验收**：`box-and-moves, references-and-mutability, scalar-reference-operators, reference-coercions, reference-lub, lifetimes-and-use, copy-clone-and-equality, runtime-value-merges, loop-state-merges, live-state-calls` 全绿

---

## P11 容器深层语义
- Vec 全部方法语义：new/push/len/index/index_mut/clone/eq
- Box：deref/eq/clone/move
- 递归 derive（`Vec<Self>` 等）
- aggregate 传参/返回/引用字段

**验收**：`vec-operations, vec-index-mutability, recursive-traits, aggregate-arguments-and-reference-fields` 全绿

---

## P12 收尾与验收
- 扫尾所有剩余 reject 用例
- 综合用例：`comprehensive-large-frame, comprehensive-owned-tree, comprehensive-quicksort, comprehensive-shortest-path, comprehensive-stack-machine`
- 全量回归 236 例
- 补 `tests/custom/` 用例
- 写 `docs/DESIGN.md`
- Code Review 演练（架构图 + 每个测试目录的实现位置 + 现场 dump/reject 演示）
- 打 tag：`ast-ok`

**验收**：`make test FILTER=official:semantic STAGE=semantic` 全绿

---

**依赖顺序**：P0 → P1 → P2 → P3 → P4 → P5 → P6/P7/P8 可交错 → P9 → P10 → P11 → P12。
