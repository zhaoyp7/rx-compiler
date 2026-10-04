# Rx 编译器作战计划（C++ + ANTLR）

> 依据 `requirement.md`：第 4 周 AST 验收、第 8 周 IR 验收、第 12 周 CodeGen 验收、第 16 周 Optimize 验收。
> 把开学第一周记为 W1；如果现在已经到第 N 周，压缩前面部分。语义分析是地基，可以压缩打磨，不能压缩正确性。
> 实现语言：Modern C++（已实测可用的工具链见第 4 节）。
> 语言规范已发布：https://acmclasscourse-2025.github.io/rx-compiler-specification/
> 官方 ANTLR g4 待代码验证后发布，`grammar/` 可能更新；同步流程见第 4 节。

---

## 0. 进度追踪

### 阶段零：环境与骨架
- [ ] 备份 `config.mk` 为 `config.reference.mk`
- [ ] 下载 ANTLR jar 到 `tools/`
- [ ] 写好 `scripts/gen_parser.sh`（改名生成 RxLexer/RxParser）
- [ ] 写好 `CMakeLists.txt`，产出 `build/rxc`
- [ ] 写好 `src/main.cpp` CLI 骨架（`--stage` / `-o`）
- [ ] 改好 `config.mk`，跑通 `make test FILTER=official:semantic:entry STAGE=semantic` 的调用链

### 阶段一：AST + 语义（W1-W4）
- [ ] 解析全部 semantic 用例源码无语法错误
- [ ] AST 节点定义完成
- [ ] `--dump-ast` 可用
- [ ] A 组语义用例绿（基础）
- [ ] B 组语义用例绿（结构体/容器）
- [ ] C 组语义用例绿（引用与可变性）
- [ ] D 组语义用例绿（递归/派生/impl）
- [ ] E 组语义用例绿（错误检查）
- [ ] F 组语义用例绿（综合）
- [ ] `make test FILTER=official:semantic STAGE=semantic` 全绿
- [ ] Code Review 材料（`docs/DESIGN.md`）

### 阶段二：IR（W5-W8）
- [ ] 安装 `clang-22`
- [ ] IR 生成器骨架（函数/基本块/终止指令/入口 wrapper）
- [ ] 表达式、控制流、函数调用
- [ ] 结构体、数组、引用、Box、Vec、derive
- [ ] `make test STAGE=ir` 全绿

### 阶段三：CodeGen（W9-W12）
- [ ] 编译 REIMU
- [ ] MIR 定义 + 指令选择 + 栈帧
- [ ] 调用约定（外部边界 ILP32）与全局数据
- [ ] Box/Vec/derive 运行时汇编
- [ ] 寄存器分配（线性扫描）
- [ ] `make test STAGE=codegen` 全绿

### 阶段四：Optimize（W13-W16）
- [ ] 常量传播 + 分支折叠
- [ ] 死代码消除
- [ ] 函数内联
- [ ] 尾递归优化
- [ ] 除法/模魔术数优化
- [ ] 寄存器分配调优
- [ ] 全量回归 + 排名冲刺

---

## 1. 先记住三件事

1. **测试集就是需求文档**。`tests/official/semantic/<主题>/*.rx` 每个目录对应一个功能点，`rej-` 文件名直接写明了拒绝理由。
2. **每个阶段的验收 = 对应测试集全绿**，不是“我觉得写完了”。
3. **Code Review 失败 = 该阶段未完成**。每阶段都要能讲清自己的设计，而不只是跑通。

评分：AST 15%、IR 15%、CodeGen 20%、Optimize 通过测试 35% + 排名 15%。

流水线与交付物：

```
.rx → ANTLR Lexer/Parser → 语法树 → 你自己建 AST → 语义分析 → ①LLVM IR(.ll) → clang → .s
                                                              └→ ②自研 MIR → RV32IM 汇编(.s) → REIMU
① 第 8 周 IR 验收   ② 第 12 周 CodeGen 验收；优化在 ② 的 MIR 上做，第 16 周验收
```

---

## 2. 资料清单（按阶段读，别一次全读）

| 阶段 | 必读 | 重点 |
|---|---|---|
| 全程 | `README-ZH.md`、`scripts/test.py`、`tests/official/manifest.schema.json` | 测试怎么调你的编译器、退出码规则、日志在哪 |
| W1 | [Rx 规范](https://acmclasscourse-2025.github.io/rx-compiler-specification/)：按 `undefined-behavior` → `tokens` → `items` → `types` → `expressions` → `grammar-summary` 顺序 | 可丢弃语法（`grammar.html`）、UB 清单（`undefined-behavior`）、名字与保护名（`names`） |
| W1 | `grammar/Lexer.g4`、`grammar/Parser.g4` | 为什么有 `condition*`/`statement*`/`closed*` 这些影子规则；lexer mode 处理 `>` `>>` `>>=` |
| W2-4 | `tests/official/semantic` 全部 `.rx` + 规范 `statements`/`expressions`/`builtin-traits`/`heap`/`const_eval` | 类型/可变性/方法解析/派生/容器的精确行为 |
| W5-8 | [LLVM LangRef](https://llvm.org/docs/LangRef.html) + [Kaleidoscope 教程](https://llvm.org/docs/tutorial/) + 规范 `backend`/`heap` | 数据类型、alloca/load/store/GEP、入口约定、runtime 接口、data layout |
| W9-12 | RISC-V psABI（ILP32）、RV32IM 指令表、`vendor/REIMU` README/docs、规范 `backend` | 外部边界寄存器/栈对齐、`.data/.rodata` 汇编、REIMU 兼容约束 |
| W13-16 | 《Engineering a Compiler》SSA/数据流/寄存器分配章节；Hacker's Delight 第 10 章 | SCCP、线性扫描、常量除法魔术数 |
| 扩展 | [Compiler for Mx* 编译器随笔](https://darksharpness.top/CompilerMx/)、[Writing a Compiler for Rust](https://norb.space/blog/writing-a-compiler-for-rust)、[compiler-101](https://github.com/ACMClassCourses/compiler-101/tree/main) | 学长学姐的架构与优化经验（注意部分技术针对旧语言/旧规范） |

### 规范关键结论（2025 版，先读，避免做多余的检查）

- **可丢弃语法**：顶层 `use` 声明和整套 lifetime 语法（参数、标注、实参、bound、where）解析后直接丢弃；不做 import 解析/别名/冲突检查，不做 lifetime 名称检查或借用检查。AST 下降时删掉即可。
- **规范列为 UB、测试不会出现的**（因此不用实现）：所有权/借用/lifetime 有效性错误、移动后使用、函数作为值、跨类型 `==`、对二进制/一元/借用运算做 expected type 传播、省略 `Box::new`/`Vec::new` 的具体类型实参、零大小数据（空结构体、零长数组、零大小字段/绑定/参数）、文档注释等。
- **仍然必须检查**：名字、类型、place 可变性（**包括不可达代码中**）；保护名（`u32/isize/usize/bool/Box/Vec/Copy/Clone/PartialEq/Eq` 不能作 struct 名；`get_i32/print_i32/println_i32` 不能作局部绑定）；局部名与可见非限定常量冲突也是 UB。
- **语言范围**：无用户 trait、枚举、模式匹配、非 unit 元组、类型参数/泛型、模块、宏、unsafe；`Box`/`Vec` 为内建；derive 只有 `Copy/Clone/PartialEq/Eq`；无整数溢出检查；常量求值仅限受限上下文。
- **IR 硬要求**：必须用 LLVM IR 作为中间表示并能输出文本 `.ll`；最终汇编必须由自研后端生成，不能调用 LLVM/Clang 代替（Clang 只能用于前端验证）。
- **入口**：导出 C ABI 的机器符号 `main`，内部调用源 `main` 后返回 0；推荐 wrapper + 内部 mangled 符号（源 `main` 的递归调用走内部符号）。
- **运行时**：I/O 内建 `get_i32/print_i32/println_i32`；堆分配 `void* __rx_alloc(u32 size, u32 align)`；**不要求释放，允许泄漏**；`bool` 1 字节、引用 4 字节、结构体字段按声明顺序等参考布局见 `backend.html`。
- **调用约定**：内部约定自由；仅机器 `main`、课程 C runtime、REIMU libc 边界必须遵守 ILP32 psABI，栈 16 字节对齐。
- **汇编**：GNU 风格文本汇编，必须同时被 Clang 集成汇编器和 REIMU 接受。
- **优化**：规范明确“No specific optimization is mandatory”；课程 `requirement.md` 另列六项必做，按课程要求执行。
- **g4 状态**：官方 g4 待发布，`grammar/` 可能更新；每次同步后重跑生成脚本与解析回归。

---

## 3. 测试运行器关键规则（来自 `scripts/test.py`）

- Semantic：编译器退出码必须 **恰好** 0（accept）或 **恰好** 1（reject）；其他退出码/信号/超时都算失败。
- IR：你的 `IR` 命令把 `.ll` 写到 `{output}`，runner 再调用 `clang-22 -S -x ir ... -O0`；每个 io 输出必须逐字节匹配期望。
- Codegen：你的 `CODEGEN` 直接产出 `.s`，runner 和 `{runtime}` 一起交给 REIMU 运行。
- Optimization：判定同 codegen；`RUN` 里给了 `{profile}` 时才会报告 REIMU cycles（不能用 `--silent`）。
- `BUILD` 每次测试只跑一次（上限 300s），必须留下 `{runtime}` 普通文件（可以空，≤8MiB）。
- 失败日志在 `target/tests/run-xxxx/NNNN/`：`.command` / `.stdout` / `.stderr`。
- `FILTER` 是 `tests` 下的目录名，用 `:` 连接；多个用 `,`；`STAGE` 可选。
- 对每个 `stage=codegen` 用例，runner 会自动补一个 `ir` 用例（所以 `STAGE=ir` 会跑所有 codegen 用例的 IR 路径）。
- `parse`/`lex` 用例被 runner 跳过，不用管。

---

## 4. W1 前半：项目骨架（工具链已实测）

系统已有 ANTLR C++ runtime 4.13.1：头文件 `/usr/include/antlr4-runtime`、静态库 `/usr/lib/libantlr4-runtime.a`。
已验证：ANTLR 4.13.2 工具 + 4.13.1 runtime 可正常工作，官方 semantic/codegen/optimization 全部 309 个用例源码可被解析。

```sh
cp config.mk config.reference.mk          # 保留 rustc 参考配置
mkdir -p tools build src/generated
# 若 /tmp/opencode/antlr/antlr-4.13.2-complete.jar 还在，直接拷；否则：
curl -fsSL -o tools/antlr-4.13.2-complete.jar https://www.antlr.org/download/antlr-4.13.2-complete.jar

# scripts/gen_parser.sh 做这件事：改名生成，避免与 antlr4::Lexer/Parser 冲突
sed 's/^lexer grammar Lexer;/lexer grammar RxLexer;/' grammar/Lexer.g4 > /tmp/RxLexer.g4
sed 's/^parser grammar Parser;/parser grammar RxParser;/; s/tokenVocab=Lexer;/tokenVocab=RxLexer;/' grammar/Parser.g4 > /tmp/RxParser.g4
java -jar tools/antlr-4.13.2-complete.jar -Dlanguage=Cpp -visitor -no-listener \
  -o src/generated /tmp/RxLexer.g4 /tmp/RxParser.g4
```

**官方 g4 发布后的同步流程**：`git diff grammar/` 看变化 → 重跑 `gen_parser.sh` → 重跑解析回归（236 个 semantic 源码零语法错误）→ 若文法有新增产生式，补 AST 节点后再继续。

**要写的东西（按顺序）：**

1. `CMakeLists.txt`：编译 `src/**/*.cpp` + `src/generated/*.cpp`，链接 `antlr4-runtime`，产物 `build/rxc`。
2. `src/main.cpp`：CLI 契约固定为 `rxc --stage semantic|ast|ir|codegen <source> [-o <output>]`；`--stage ast` 打印 AST。语法错误（自定义 `BaseErrorListener` 计数）或语义错误 → **exit 1**，成功 → 0。
3. `.gitignore`：加 `build/`、`src/generated/`、`tools/*.jar`、`target/`。
4. `config.mk` 改成（`BUILD` 负责构建你的编译器并留下空的 `{runtime}`；IR/CodeGen 阶段再考虑编译课程 C runtime 到 `{runtime}`）：
```make
BUILD = cmake --build build -j32 >/dev/null && : > {runtime}
SEMANTIC = ./build/rxc --stage semantic {source}
IR = ./build/rxc --stage ir {source} -o {output}
CODEGEN = ./build/rxc --stage codegen {source} -o {output}
RUN = xmake run -P vendor/REIMU reimu --memory=256M --stack=1M -f {output},{runtime} -o {stdout} -p {profile} 1>&2
```
5. 自测：先写个恒返回 1 的 `--stage semantic` 桩，跑 `make test FILTER=official:semantic:entry STAGE=semantic`，确认脚手架能调用你。

git 习惯：`main` 永远可编译；每个阶段开 `stage-ast` / `stage-ir` / … 分支；验收前打 tag。

---

## 5. W1-W4：AST + 语义分析（最关键的一阶段）

### 每周任务

| 周 | 读 | 写 | 验收命令 |
|---|---|---|---|
| W1 | Rx 规范（先读 `undefined-behavior`/`grammar`）、g4、test.py | 解析跑通；AST 节点定义；`RxParserBaseVisitor` → AST；丢弃 `use`/lifetime；`--dump-ast` | 能解析 semantic 全部源码无语法错误 |
| W2 | 规范 `names`/`const_eval`；`entry`、`names-and-shadowing`、`namespace-errors` 用例 | 全局符号表、作用域链、名字解析、重复定义、受限常量求值、保护名 | `make test FILTER=official:semantic:entry,official:semantic:names-and-shadowing,official:semantic:namespace-errors,official:semantic:constant-errors STAGE=semantic` |
| W3 | `integer-arithmetic`、`casts-and-literals`、`boolean-and-short-circuit`、`loops-and-jumps`、`structs-and-fields`、`methods-and-self` 用例 | 类型系统、运算符检查、if/loop/break/continue、never 类型、结构体字段、方法解析 | A 组 + B 组（见下） |
| W4 | 剩余所有 semantic 用例；规范 `heap`/`builtin-traits`/`references` | 引用与 place 可变性、Copy 分类、Box/Vec/derive/impl/递归布局；补齐边界 | `make test FILTER=official:semantic STAGE=semantic` 全绿，准备 Review |

### 语义测试的攻克顺序（由易到难）

```sh
# A 组：基础
official:semantic:entry,names-and-shadowing,integer-arithmetic,shifts,casts-and-literals,
constants-and-paths,constant-errors,boolean-and-short-circuit,blocks-if-and-never,
loops-and-jumps,compound-assignment,source-main-calls
# B 组：结构体/容器
official:semantic:structs-and-fields,methods-and-self,builtin-io,box-and-moves,arrays,
vec-operations,vec-index-mutability,nested-containers,expected-types,evaluation-order-and-temporaries
# C 组：引用与可变性
official:semantic:references-and-mutability,scalar-reference-operators,reference-coercions,
reference-lub,lifetimes-and-use,copy-clone-and-equality,runtime-value-merges,
loop-state-merges,live-state-calls,aggregate-arguments-and-reference-fields
# D 组：递归/派生/impl
official:semantic:recursive-layout,recursive-traits,trait-dispatch-and-reference-equality,invalid-impls-and-generics
# E 组：错误检查
official:semantic:namespace-errors,protected-names,unreachable-checks
# F 组：综合，最后做
official:semantic:comprehensive-*
```

### 设计要点（教程部分）

- **AST 与语法树分离**：`condition*` / `statement*` / `closed*` 只是 ANTLR 消歧用的影子规则，你的 AST 里不存在这些区分，统一成 `BinaryExpr`、`IfExpr`、`BlockExpr` 等；从语法树下降时顺便剥掉语法糖。
- **解析后丢弃 `use` 与 lifetime**：不建 import 表、不做别名/冲突/生命周期检查；规范保证这些用例不会出现。
- **节点带 span**（起止偏移 + 行列），语义报错全靠它。
- **TypeRef（语法）与 Type（语义）分离**：`[i32; 3]`、`&mut T` 先建成语法结构，再在语义阶段解析成规范化类型。
- **类型用 interning**：类型指针唯一化，判等就是指针比较；结构体布局在此阶段算好（偏移/大小/对齐），递归布局（值包含自身）报错。
- **两遍扫描**：第一遍收集顶层 item（函数/结构体/常量/impl），第二遍检查函数体。
- **不需要所有权/借用/lifetime 检查器**：移动后使用、借用冲突都不必报错（规范列为 UB，测试不出现）。但仍要实现 **place 可变性检查**（赋值/可变借用/`&mut self` receiver，含不可达代码），以及 `Copy` 分类——它用于数组重复字面量规则和后续代码生成选择 copy/move。
- **方法解析顺序**：固有 `impl` 方法优先于 derive 生成的 `clone/eq`；点调用可能要自动引用/解引用一层；“多个不同方法同时匹配”属于 UB，不用处理歧义。
- **运算符即能力**：`==`/`!=` 要求 `PartialEq` 且两侧同源类型（跨类型相等是 UB）；`+=` 等复合赋值要求可变左值。
- **推导与 expected type**：整数字面量按“后缀 → expected type → `i32` 兜底”选型；规范保证不会要求 expected type 穿透二元/一元/借用运算；`if/else` 两分支求公共类型；`return`/`break`/`continue` 是 never 类型。
- **零大小数据**：空结构体、零长数组、零大小字段/绑定/参数都是 UB，可直接拒绝或不做支持。
- 文法里 `genericParams` 只接受生命周期参数，说明用户自定义类型泛型被裁掉了；`Box`/`Vec` 是内建泛型类型。

---

## 6. W5-W8：IR（输出 LLVM IR 文本）

**策略**：语义分析后给 AST 每个表达式挂上类型和符号引用，然后直接发 `.ll` 文本。**不要**先造 SSA，全部变量用 `alloca`+`load/store`，靠正确性过关。

| 周 | 写 | 验收 |
|---|---|---|
| W5 | 装 `clang-22`；IR 文本生成器骨架：target triple/datalayout、函数、基本块、终止指令、机器 `main` wrapper | 手写/生成一个 hello `.ll`，clang 编译后 REIMU 跑通 |
| W6 | 表达式、控制流（if/while/loop/break/continue）、函数调用、I/O 内建 | `make test STAGE=ir FILTER=official:codegen:integer-arithmetic` |
| W7 | 结构体、数组、引用（指针表示）、Box（`__rx_alloc`）、Vec 运行时、derive 的 clone/eq | `official:codegen:arrays,structs-and-fields,vec-operations` |
| W8 | 边界补齐、全量回归 | `make test STAGE=ir` 全绿 |

**关键约定：**
- **入口**：生成机器符号 `main`（C ABI），内部调用 mangled 的源 `main` 后返回 0；源 `main` 的递归调用走内部符号。
- **IR 必须合法**：target triple / data layout 与 RV32IM/ILP32 一致；用 `clang-22 -S -x ir ... -O0` 自检。
- **运行时二选一**：直接 `declare` REIMU libc 的 `printf`/`scanf`/`malloc`，或把规范附带的参考 C runtime（`get_i32/print_i32/println_i32/__rx_alloc`）编译进 `{runtime}`；**不需要 free，允许泄漏**。
- 字面量字符串放 `private constant [N x i8]`；`%d\n` 之类格式串需要正确转义 `\00`。
- Box：`{ptr}`，new = `__rx_alloc` + store，`*b` = load/store。
- Vec：layout 自定（建议 `{ptr, i32 len, i32 cap}`），把 `new/len/is_empty/push/remove/index/clone/eq` 写成手写 IR 运行时前置模块；**这套 layout 到 CodeGen 阶段用汇编再实现一遍**，语义保持一致。
- 所有基本块必须有终止指令；`alloca` 建议集中放入口块。
- 调试手法：把生成的 `.ll` 单独跑 `clang-22 -S ...` 看报错；再用参考 rustc 配置生成参考 `.ll` 对照（`config.reference.mk` 单独放一个 worktree/branch 跑）。
- `clang-22` 安装：LLVM apt 源（apt.llvm.org）或官方 release，确保 `clang-22` 在 PATH 上。

---

## 7. W9-W12：CodeGen（自研 RV32IM 后端）

**策略**：引入自己的中端 IR（MIR）：函数 → 基本块 → 三地址指令（虚拟寄存器 + 栈槽）。先做“全部 spill 到栈”的朴素版本保证正确，再上寄存器分配。

| 周 | 写 | 验收 |
|---|---|---|
| W9 | MIR 定义；AST/HIR → MIR；指令选择（RV32IM 表）；栈帧/序言/尾声；机器 `main` wrapper | `make test STAGE=codegen FILTER=official:codegen:integer-arithmetic,loops-and-jumps` |
| W10 | 外部边界调用约定（ILP32）、全局数据、字符串、I/O 内建 | `official:codegen:calls-recursion-and-abi,builtin-io` |
| W11 | Box/Vec/derive 运行时汇编（`__rx_alloc`/内存拷贝，无需 free）；数组与结构体 | `official:codegen:arrays,structs-and-fields,vec-operations` |
| W12 | 寄存器分配（线性扫描）、边界修复、全量回归 | `make test STAGE=codegen` 全绿 |

**关键细节：**
- REIMU 没有子模块编译，先构建：`xmake f -y -P vendor/REIMU -m release -o target/reimu && xmake -y -P vendor/REIMU`。
- **内部调用约定自由**；只有机器 `main`、课程 C runtime、REIMU libc 边界必须遵守 psABI（a0-a7 传参、a0 返回、s0-s11 保留、栈 16 字节对齐）。
- 有符号/无符号运算在语义阶段已确定，选择 `div/divu/rem/remu`、`slt/sltu`、`srl/sra` 不要混。
- 分支用 `beq/bne/blt/bge/bltu/bgeu` + `j/jal/jalr`；比较结果用 `slt + xori`。
- 全局：`.globl main`、`.section .data/.rodata/.bss`、`.align`；地址用 `lui/addi` 或 `auipc/addi`。
- **汇编兼容性**：输出的 GNU 风格文本汇编必须同时被 Clang 集成汇编器和 REIMU 接受；Clang 能收但 REIMU 不认的语法不要用。
- 运行时汇编和你的 MIR 的 ABI 必须一致（参数、返回值、对齐），先写死一个约定并记录在 `docs/DESIGN.md`。
- 调试：失败用例日志在 `target/tests/run-xxxx/NNNN/`，有 `.command/.stdout/.stderr`；REIMU 的 trace 模式可以单步查。

---

## 8. W13-W16：Optimize（课程要求六项）

规范明确“No specific optimization is mandatory”，但 `requirement.md` 要求六项必做，按课程要求执行。
在 MIR 上按这个顺序上，每加一个 pass 都跑两遍测试：`official:optimization` 看周期，`official:codegen` 保正确。

1. **常量传播 + 分支折叠**（worklist；等价于简化版 SCCP，不做 SSA 也行）。
2. **死代码消除**（从有副作用的指令反向标记，扫掉未标记）。
3. **函数内联**（小函数、单调用点优先；带递归/增长阈值保护）。
4. **尾递归优化**（识别 `call self` 后直接 `ret`，改写为循环）。
5. **除法/模魔术数优化**（除以常量 → `mulhi + shift + 修正`；注意负数、`INT_MIN`、除数为 2 的幂、`-1`；有符号/无符号分开）。
6. **寄存器分配**（全局线性扫描：数据流求活跃区间 → 扫描分配 → spill；把热循环变量优先留在寄存器里）。

| 周 | 目标 |
|---|---|
| W13 | 1+2 完成，周期明显下降 |
| W14 | 3+4 完成 |
| W15 | 5+6 完成，重点调 `matrix-multiply / jacobi-stencil / floyd-warshall / knapsack` |
| W16 | 全量回归 + 热点调优 + 排名冲刺 |

测量方式：`make test FILTER=official:optimization` 会打印每个 io 的 REIMU cycles 并生成 `optimization-cycles.json`（`RUN` 里的 `{profile}` 不能省，也不要用 `--silent`）。

---

## 9. 每周固定动作（防翻车）

- **给助教发答疑邮件**：课程强制，即使本周没有问题也要发空记录并说明。
- **测试驱动**：改完先跑受影响子集；每天至少一次 `make test FILTER=official:semantic STAGE=semantic`（很快）。
- **自己加用例**：`tests/custom/`，文件名建议 `acc-*` / `rej-*`，runner 自动发现。
- **留证据**：`docs/DESIGN.md` 记录 IR/MIR/ABI/优化 pass 的设计与取舍，Code Review 直接用它讲。
- **AI 规范**：代码必须逐行自己写（整段 AI 生成该部分不得分）；可以用 AI 讲概念、审设计、帮定位 bug。
- **CI**：前期把 `.github/workflows/test.yml` 改名加 `.disabled` 禁用，验收前再启用。

---

## 10. 总排期表

| 周 | 里程碑 | 一句话目标 |
|---|---|---|
| W1 | 骨架 + AST 雏形 | 解析全部用例，能 dump AST |
| W2 | 名字/常量 | A 组语义用例绿 |
| W3 | 类型/控制流/结构体 | B 组绿，方法解析正确 |
| W4 | **AST 验收** | `official:semantic` 全绿 + Code Review |
| W5-6 | IR 基础 | 表达式/控制流 IR 跑通 |
| W7 | IR 高级 | 结构体/数组/Box/Vec IR 跑通 |
| W8 | **IR 验收** | `make test STAGE=ir` 全绿 |
| W9-10 | 后端基础 | 指令选择 + 调用约定 |
| W11 | 运行时 | Box/Vec/derive 汇编运行时 |
| W12 | **CodeGen 验收** | `make test STAGE=codegen` 全绿 |
| W13-15 | 优化 | 六项必做逐个落地并量化 |
| W16 | **Optimize 验收** | 全量测试绿 + 周期排名冲刺 |

---

## 11. 命令速查

```sh
# 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j32

# 只跑语义（快）
make test FILTER=official:semantic STAGE=semantic
make test FILTER=official:semantic:arrays STAGE=semantic

# IR / CodeGen / 优化
make test STAGE=ir
make test STAGE=codegen FILTER=official:codegen:arrays
make test FILTER=official:optimization

# 显示每个用例
make test VERBOSE=true
# 放宽超时
make test FILTER=official:optimization COMPILE_TIMEOUT=60 RUN_TIMEOUT=30

# 构建 REIMU（CodeGen 前必须）
xmake f -y -P vendor/REIMU -m release -o target/reimu && xmake -y -P vendor/REIMU

# 规范推荐的前端独立验证（IR 阶段）
clang --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -O0 -S student.ll -o frontend-check.s
clang --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -O2 -fno-builtin -S runtime-example.c -o runtime.s
# 用 REIMU 一起运行 frontend-check.s 与 runtime.s 并比对输出
```

---

**现在立刻做的三件事**：① 按第 4 节搭骨架并跑通 `semantic:entry` 的调用链；② 按规范顺序通读 `undefined-behavior` → `tokens` → `items` → `types` → `expressions`，并看 `entry`/`names-and-shadowing` 全部 `.rx`；③ 写 AST 节点定义和 `--dump-ast`（记得丢弃 `use`/lifetime）。
