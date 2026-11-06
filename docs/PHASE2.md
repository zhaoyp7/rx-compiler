# 阶段二（IR）执行手册

> 对应第 8 周 IR 验收；范围严格按 `docs/PLAN.md` §6 的 W5-W8：语义分析结果 → LLVM IR 文本 `.ll` → clang-22 → REIMU。
> 前置：阶段一已完成，`official:semantic` 236/236、`tests/custom` 122/122，语义层冻结。
> 编写约定：每个任务给出 **做法**（具体怎么下手）和 **作用**（为什么需要它、它影响后面什么）。
> 编译器代码自己写；本手册只描述方法和原因，遇到设计取舍卡壳再来讨论。
> 本阶段不包含自研后端与优化；CodeGen、Optimize 留到后续阶段。

---

## 0. 总览

```
.rx
 │ 阶段一：ANTLR → AST → 语义分析（已冻结）
 ▼
符号表 / 作用域 / TypeId / 布局 / 常量 / 表达式类型旁表
 │
 │ 阶段二：本手册
 ▼
LLVM IR（内存表示）──► .ll 文本 ──► clang-22 ──► RV32IM 汇编
 │
 ▼
REIMU（与 {runtime} 链接）──► stdout 逐字节比对
```

### 0.1 测试合同

| 项目 | 内容 |
|---|---|
| 命令 | `IR = ./build/rxc --stage ir {source} -o {output}` |
| 测试来源 | runner 为每个 `stage=codegen` 的 manifest 条目自动补一个 `ir` 用例 |
| 规模 | `tests/official/codegen/` 下 38 个目录、**60 个用例、115 组 I/O** |
| 判定 | 编译器退出码 0 且生成 `{output}`；clang-22 能编译该 `.ll`；REIMU 输出与期望逐字节一致 |
| 命令 | `make test STAGE=ir` |

runner 在 IR 阶段实际做两步：

```sh
# 1. 你的编译器生成 .ll
./build/rxc --stage ir foo.rx -o program.ll
# 2. runner 用 clang-22 把 .ll 编成 .s，再交给 REIMU
clang-22 -S -x ir program.ll -o program.s \
  --target=riscv32-unknown-none-elf -march=rv32im -mabi=ilp32 -O0 \
  -mllvm -riscv-no-aliases
```

注意：`{runtime}` 默认由 `BUILD` 留空；I/O 和 `__rx_alloc` 要么由 `{runtime}` 提供，要么由你的 `.ll` 自己实现。这是 I0 要定的第一个设计决策。

### 0.2 当前现状

- `clang-22` **不在 PATH**，W5 第一件事就是安装并确认。
- `vendor/REIMU` 尚未构建；IR 阶段虽然不写后端，但需要用 REIMU 跑 clang 生成的汇编。
- `config.mk` 中 `IR =` 为空，`BUILD` 只会留下空 `{runtime}`。
- `src/main.cpp` 只处理 `parse/ast/semantic`；`--stage ir` 和 `-o` 还没接。
- `driver.h` 已有 `hasOutput/output`，但 main 尚未使用。
- 语义层已冻结：任何 semantic 改动都要回归 236 + 122。
- OJ 的 `BUILD` 用 `-j2`，不要改回 `-j32`；`src/generated/` 已提交，OJ 不需要 Java。
- `vendor/REIMU` 里的未跟踪内容不要 `git add .` 带进来。

### 0.3 阅读清单

| 材料 | 关注点 |
|---|---|
| `rx-spec-zh/16-backend/16.00-backend.md` | **全篇精读**：target `riscv32`、RV32IM/ILP32；数据布局表；psABI 边界；机器 `main` wrapper；I/O 的 C ABI；`__rx_alloc` 契约 |
| `rx-spec-zh/15-builtin/15.00-builtin.md` | 源 `main` 签名与入口行为；`get_i32 / print_i32 / println_i32` 的精确语义 |
| `rx-spec-zh/14-heap/14.00-heap.md` | Box/Vec 全部内建操作；索引/重借语义；`clone`/`eq`；允许泄漏；`__rx_alloc` 的 size/align 规则 |
| `rx-spec-zh/11-expressions/11.00-expressions.md` | 求值顺序；place/value；**临时量具现化** |
| `rx-spec-zh/11-expressions/11.04-operator-expr.md` | 二进制补码回绕；有符号除法截断；移位掩码 5 位；短路逻辑；赋值/复合赋值求值顺序 |
| `rx-spec-zh/11-expressions/11.07 ~ 11.14` | 数组/索引、结构体字面量、调用、方法调用、字段、loop/while、if、return |
| `rx-spec-zh/10-types/10.00 ~ 10.03` | 转换与 LUB；数组/引用/Box/Vec 的类型与表示 |
| `rx-spec-zh/09-statements/09.00-statements.md` | `let`、表达式语句、块边界 |
| `rx-spec-zh/08-traits-and-attributes/08.01-builtin-traits.md` | Copy/Clone/PartialEq/Eq 能力与 derive 规则 |
| `rx-spec-zh/13-references/13.00-references.md` | 引用用地址表示；存储期与实现自由度 |
| `rx-spec-zh/01-undefined-behavior/01.00-undefined-behavior.md` | 明确不用实现的项、零大小数据、测试保证 |
| `docs/PLAN.md` §3、§6 | runner 规则、W5-W8 周计划与 IR 关键约定 |
| LLVM LangRef | `alloca/load/store/GEP`、aggregate、`global`、`define/declare`、data layout，只需子集 |

### 0.4 周计划

| 周 | 任务 | 验收 |
|---|---|---|
| W5 | I0 环境与接口；I1 IR 骨架/类型/入口 | 手写/生成 hello `.ll`，clang-22 + REIMU 跑通 |
| W6 | I2 标量表达式；I3 控制流；I4 调用/I/O | `integer-arithmetic`、`shifts`、`loops-and-jumps`、`builtin-io` 等 |
| W7 | I5 聚合类型；I6 引用/place；I7 Box/Vec/derive | `arrays`、`structs-and-fields`、`vec-operations` 等 |
| W8 | I8 边界补齐、全量回归 | `make test STAGE=ir` 60/60 |

---

## I0 环境与接口准备

**为什么先做这一步**：IR 阶段有三条外部依赖——clang-22、REIMU、runtime。它们和 CLI/`config.mk` 没打通之前，任何 IR 生成错误都会被误判成环境问题。

1. **安装并验证 clang-22**
   - 做法：从 apt.llvm.org 或官方 release 安装；跑 `command -v clang-22`、`clang-22 --version`、`clang-22 -print-targets | grep riscv`。
   - 作用：runner 固定用 `clang-22 -S -x ir ...`；没有它，IR 阶段一个用例都跑不了。
   - 注意：runner 的目标是 `riscv32-unknown-none-elf`，规范文字写的是 `riscv32-unknown-elf`；两者 ILP32 布局相同。你的 triple 选定后固定下来并记录。

2. **构建 REIMU**
   - 做法：
     ```sh
     xmake f -y -P vendor/REIMU -m release -o target/reimu
     xmake -y -P vendor/REIMU
     xmake run -P vendor/REIMU reimu --help
     ```
   - 作用：IR 阶段虽然用 clang 生成汇编，但最终要放进 REIMU 跑，才能验证行为。

3. **接 CLI：`--stage ir -o`**
   - 做法：stage 解析增加 `ir`；`ir` 必须要求 `-o`；生成成功写文件并 exit 0，失败 exit 1。
   - 作用：runner 只认 `{output}` 文件；退出 0 但没文件也判失败。
   - 注意：语法/语义错误仍 exit 1；生成阶段的内部错误也不能以 0 退出或崩溃。

4. **接 `config.mk`**
   - 做法：
     ```make
     IR = ./build/rxc --stage ir {source} -o {output}
     ```
     `BUILD` 保持 `cmake --build build -j2`；若用 C runtime，则同时编译 `{runtime}`：
     ```make
     BUILD = cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null && \
         cmake --build build -j2 >/dev/null && \
         clang-22 --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 \
           -O2 -fno-builtin -S runtime/runtime.c -o {runtime}
     ```
   - 作用：runner 的每条命令都从 `config.mk` 读；`{runtime}` 每轮测试只构建一次。
   - 注意：`{runtime}` 必须是普通文件且 ≤8MiB；`BUILD` 上限 300s；暂时不用 C runtime 就先 `: > {runtime}`。

5. **定 runtime 策略（第一个设计决策）**
   - 方案 A：写 `runtime/runtime.c`，提供 `get_i32/print_i32/println_i32/__rx_alloc`，由 `BUILD` 编译成 `{runtime}`。
   - 方案 B：`.ll` 里直接声明 REIMU libc 的 `scanf/printf/malloc`，自己实现 I/O 和 alloc。
   - 建议：IR 阶段用方案 A，ABI 清楚、代码集中；规范明确允许 Clang 编译 C runtime。
   - 注意：runtime 不能定义 `main`；I/O 和 `__rx_alloc` 符号只能出现一次。

6. **冻结语义接口**
   - 做法：在 `SemanticAnalyzer` 上增加只读访问器，暴露 IR 需要的旁表：`exprTypes`、`structLayouts`、`fnParamTypes`、`fnReturnTypes`、`fnReceiverTypes`、`fnSymbols`、`constValues`、`types`、`symbols`、impl/方法表、`isCopy`。
   - 作用：IR 直接读语义结果，不重复推导类型、布局和常量；避免把语义字段挂回 AST。
   - 注意：只加访问器、不改语义行为；改完立刻回归 236 + 122。

7. **建目录与调试开关**
   - 做法：建 `src/ir/`；可选 `--dump-ir` 只在调试时打印，不影响正常输出。
   - 作用：阶段隔离；CMake 用 `GLOB_RECURSE CONFIGURE_DEPENDS`，新增 `.cpp` 一般能自动发现，异常时手动 `cmake -S . -B build`。
   - 注意：提交只加具体文件，不要 `git add .`。

**验收**：
- `command -v clang-22` 有输出；`xmake run -P vendor/REIMU reimu --help` 正常。
- `./build/rxc --stage ir <file> -o /tmp/x.ll; echo $?` 能产出文件。
- `make test FILTER=official:semantic STAGE=semantic` 仍 236/236；`make test FILTER=custom STAGE=semantic` 仍 122/122。
- `BUILD` 跑完后 `{runtime}` 存在且不超过 8MiB（若采用方案 A）。

**常见坑**：`BUILD` 里既构建编译器又构建 runtime，任一失败整轮测试失败；`clang-22` 的 target 写错会得到 unknown target；`-o` 目录不存在时要在编译器里创建或明确报错。

---

## I1 IR 骨架、类型与入口

**为什么做**：先建立“能跑通的最小闭环”，再往里填表达式。triple、data layout、基本块终止规则、入口 wrapper 一旦定错，后面所有用例都会以同样方式失败。

1. **确定 IR 的内存表示**
   - 做法：建 `Module / Function / BasicBlock / Instruction / Value` 的小型 IR；值用递增编号（`%t0`、`%t1`），符号用 `@name`；文本输出集中在一个 emitter。
   - 作用：同一份内存 IR 既生成 `.ll`，也为后续阶段复用；避免“边遍历 AST 边拼字符串”导致后面被迫重写。
   - 注意：这一步不需要 SSA/phi；变量统一 `alloca + load/store`。

2. **模块头：triple 与 data layout**
   - 做法：
     ```llvm
     target triple = "riscv32-unknown-elf"
     target datalayout = "e-m:e-p:32:32-i64:64-n32-S128"
     ```
     用 clang-22 实测确认；若统一用 runner 的 `riscv32-unknown-none-elf`，就固定后者并记录。
   - 作用：保证 `i32/usize/ptr` 都是 32 位、小端；这是布局和 ABI 的前提。

3. **类型系统映射**
   - 做法：
     - 整数：`i32`；`bool` 的 SSA 值用 `i1`，内存中用 `i8`。
     - 引用/指针：`ptr`（LLVM 22 默认 opaque pointer）。
     - 数组：`[N x T]`；结构体：`%struct.Name = type { ... }`。
     - `()`：不占存储；函数返回 `void`。
   - 作用：与语义层 `TypeId` 一一对应；布局直接复用 `structLayouts`。
   - 注意：不要用 `i32` 存 `bool`，否则结构体布局会和语义层不一致。

4. **函数声明/定义与基本块**
   - 做法：先发射所有函数签名（`declare`/`define`），再发射函数体；每个函数至少一个入口块；所有块必须以 `ret/br/unreachable` 终止。
   - 作用：支持前向引用、互相递归；clang 对“块没有终止指令”会直接报错。

5. **机器 `main` wrapper**
   - 做法：
     ```llvm
     define i32 @main() {
     entry:
       call void @rx_main()
       ret i32 0
     }
     ```
     源 `main` 用唯一内部符号（如 `@rx_main` 或 mangling 后的名字）；源内对 `main` 的递归调用必须指向内部符号。
   - 作用：满足 C ABI 入口；递归不会绕回 wrapper；wrapper 返回 0。
   - 注意：内部符号不能和 runtime/REIMU 符号冲突；不要导出两个 `main`。

6. **`alloca` 位置**
   - 做法：所有局部变量和临时量的 `alloca` 集中在函数入口块；循环体内只做 `load/store`。
   - 作用：避免循环里栈持续增长；`alloca` 在入口块也更容易被 clang/REIMU 处理。

7. **最小端到端**
   - 做法：先手写一个只调用 `println_i32(42)` 的 `.ll`，用 clang-22 + REIMU 跑通；再让 `--stage ir` 生成同构文件。
   - 作用：把“IR 语法、runtime 链接、REIMU 参数”三件事一次排干净。

**验收**：`make test STAGE=ir FILTER=official:codegen:builtin-io` 至少能进入 clang/REIMU 阶段；手写 hello `.ll` 输出正确。
**常见坑**：`alloca` 放在非入口块；`ret void` 和 `ret i32` 混用；块名/值名重复；`main` 重复定义。

---

## I2 标量表达式与运算

**为什么做**：整数、布尔、移位、比较覆盖了 60 个用例里最广的一层；这里错了，控制流和容器都无从谈起。

1. **字面量与类型后缀**
   - 做法：按语义层已算好的 `exprTypes` 直接发 `i32` 常量；`bool` 发 `i1 true/false`；负数用 `sub i32 0, x` 或负常量。
   - 作用：避免在 IR 阶段重新做整数字面量选型。

2. **算术/位运算**
   - 做法：`add/sub/mul/sdiv/udiv/srem/urem/and/or/xor`，按语义层的有符号性选指令。
   - 关键：**不要给 `add/sub/mul` 加 `nsw/nuw`**；源语言要求二进制补码回绕。
   - 作用：`integer-arithmetic` 的边界/回绕用例。

3. **移位**
   - 做法：左操作数类型决定 `shl/lshr/ashr`；右操作数先 `and i32 %rhs, 31` 再移位；常量折叠也按 5 位掩码。
   - 作用：RISC-V 硬件只取低 5 位；LLVM IR 对 ≥32 的移位量行为不同，必须显式掩码。
   - 注意：有符号右移 `ashr`，无符号右移 `lshr`。

4. **比较**
   - 做法：`icmp eq/ne/slt/sle/sgt/sge/ult/ule/ugt/uge`；结果 `i1`；需要存成 `i32` 时 `zext`。
   - 作用：条件分支、短路逻辑、`Eq` derive 的基础。

5. **一元与强转**
   - 做法：`-x` → `sub i32 0, x`；`!x` → 布尔 `xor i1 x, true` 或整数 `xor i32 x, -1`；`as` 按语义层允许的组合发 `trunc/zext/sext`（32 位整数之间多为空操作）。
   - 作用：`casts-and-literals`、`scalar-reference-operators`。

6. **赋值与复合赋值**
   - 做法：先 `lowerPlace(lhs)` 得到地址（只求值一次），再 `load`、运算、`store`；整个表达式类型为 `()`。
   - 作用：`compound-assignment`、`evaluation-order-and-temporaries`。
   - 注意：`a[i()] += f()` 中 `i()` 只能调用一次；地址先算好存临时。

**验收**：`make test STAGE=ir FILTER=official:codegen:integer-arithmetic,official:codegen:shifts,official:codegen:casts-and-literals,official:codegen:compound-assignment` 全绿。
**常见坑**：有符号/无符号指令混用；移位不掩码；给回绕运算加 `nsw`；`i1` 直接当 `i32` 用。

---

## I3 控制流、短路与状态合并

**为什么做**：if/while/loop/break/continue/return 是所有程序骨架；短路逻辑还要求“右侧只在需要时求值”，不能用按位运算代替。

1. **if/else**
   - 做法：`then/else/merge` 三块；`br i1`；有结果时在入口块开临时 `alloca`，两支分别 `store`，merge 后 `load`。
   - 作用：if 表达式、`blocks-if-and-never`；无 else 时结果必须是 `()`。
   - 注意：两个分支都 `Never` 时后续块可以不可达，但仍要有合法终止。

2. **while/loop**
   - 做法：`cond/body/exit` 三块；`break` 跳到 exit，`continue` 跳到 cond；`loop` 的 `break` 可带值，用临时槽承接；`while` 的 break 不带值。
   - 作用：`loops-and-jumps`。
   - 注意：`continue` 在 `while` 里必须重新判断条件，不能直接跳 body。

3. **短路 `&&` / `||`**
   - 做法：`lhs` 求值 → `br` → `rhs` 块或 merge 块；右侧的值写入 `i1` 临时槽，merge 后 load。
   - 作用：`boolean-and-short-circuit` 会检查右侧副作用是否被跳过。
   - 注意：绝对不能用 `and/or` 指令“先算两边”。

4. **return 与不可达**
   - 做法：`return e` → `ret` 对应类型；`return;` → `ret void`；`Never` 路径用 `unreachable`；不可达的源语句可以不再发射，但语义阶段已经检查过它们。
   - 作用：函数末尾不会缺终止指令；`blocks-if-and-never`。

5. **块表达式与尾表达式**
   - 做法：块的值放在临时槽；有尾表达式则 store，无尾表达式则 `()`。
   - 作用：函数体、if 分支、loop body 的统一。

6. **状态合并**
   - 做法：所有跨块存活的值都通过 `alloca` 槽传递，不要求 phi；循环变量每轮 load/store。
   - 作用：`live-state-calls`、`loop-state-merges`、`runtime-value-merges`；朴素但正确。

**验收**：`make test STAGE=ir FILTER=official:codegen:boolean-and-short-circuit,official:codegen:blocks-if-and-never,official:codegen:loops-and-jumps,official:codegen:live-state-calls,official:codegen:loop-state-merges,official:codegen:runtime-value-merges` 全绿。
**常见坑**：短路右侧被无条件求值；`loop` 的 break 值丢失；`continue` 跳错块；merge 块没有终止指令。

---

## I4 函数、方法与 I/O

**为什么做**：调用是程序和 runtime 的唯一接口；方法 receiver、单元返回、递归和参数求值顺序都在这里定型。

1. **函数签名与参数**
   - 做法：参数类型/返回类型直接读 `fnParamTypes/fnReturnTypes`；`()` 返回用 `void`；参数按源码顺序求值后传入。
   - 作用：`calls-recursion-and-abi`。

2. **聚合参数的约定（先定死）**
   - 做法：IR 层可以 first-class aggregate 按值传/返；也可以统一传“指向调用者临时量的指针”，被调者在入口复制到自己的 `alloca`。二选一，写进 `docs/DESIGN.md`。
   - 建议：IR 层用 first-class aggregate，更直观，也符合“数组不退化”的规范。
   - 作用：`aggregate-arguments-and-reference-fields`；避免 callee 修改 caller 的值。

3. **方法 lowering**
   - 做法：把 receiver 降成显式第一参数：`self` 按值、`&self`/`&mut self` 传 `ptr`；语义层已完成自动引用/解引用，IR 只按结果取地址或 load。
   - 作用：`methods-and-self`。

4. **I/O 内建**
   - 做法：声明
     ```llvm
     declare i32 @get_i32()
     declare void @print_i32(i32)
     declare void @println_i32(i32)
     ```
     调用后忽略 `()` 返回值。
   - 作用：`builtin-io`；与 `{runtime}` 的 C ABI 对齐。

5. **源 `main` 调用**
   - 做法：所有源级 `main()` 调用指向内部符号；wrapper 只调用一次。
   - 作用：`source-main-calls` 的递归用例。

6. **调用现场的求值顺序**
   - 做法：实参从左到右求值，先算完存临时，再统一放参数位置；不要边算边覆盖已用的参数槽。
   - 作用：`evaluation-order-and-temporaries`。

**验收**：`make test STAGE=ir FILTER=official:codegen:calls-recursion-and-abi,official:codegen:builtin-io,official:codegen:methods-and-self,official:codegen:source-main-calls` 全绿。
**常见坑**：`&self` 传成了值；unit 函数返回 `i32`；wrapper 和源 main 重名；实参求值顺序颠倒。

---

## I5 结构体、数组与按值语义

**为什么做**：聚合类型是容器和综合用例的主体；“按值”不能退化成指针，否则 callee 的修改会污染 caller。

1. **结构体布局**
   - 做法：用语义层 `structLayouts` 的字段顺序/offset/size/align 发射 `%struct.Name`；字段按声明顺序。LLVM 自然布局应与规范一致，若不一致就显式加 padding 字段。
   - 作用：`structs-and-fields`、`aggregate-arguments-and-reference-fields`。

2. **构造与字段访问**
   - 做法：结构体字面量按**源码顺序**求值字段，再 `store` 到 `alloca`；字段访问用 `getelementptr %struct.T, ptr %p, i32 0, i32 idx`；place 返回地址，value 再 `load`。
   - 作用：字段初始化顺序、`evaluation-order-and-temporaries`。

3. **数组**
   - 做法：`[N x T]`；元素 `i` 的地址是 `getelementptr [N x T], ptr %p, i32 0, i32 i`；列表逐元素 store；重复形式 `[e; N]` 只求值 `e` 一次，再复制 N 次。
   - 作用：`arrays`；N==1 不要求 Copy，N>1 语义层已保证 Copy。

4. **按值复制**
   - 做法：赋值/传参/返回聚合时整块 `load/store` 或用 `memcpy`；必须复制到独立存储（`alloca` 或调用者临时），不能只传指针。
   - 作用：`arrays` 的“copy arrays remain independent”、结构体按值参数。
   - 注意：填充字节值未指定，复制整块即可；不要让填充字节影响可观察行为。

5. **嵌套聚合**
   - 做法：递归 GEP；`Vec<Vec<i32>>` 的内层是堆上的动态数组，不走 `[N x T]`。
   - 作用：`nested-containers`。

6. **索引边界**
   - 做法：不做越界检查（UB，测试排除）；直接 GEP。
   - 作用：省掉分支和 panic 运行时。

**验收**：`make test STAGE=ir FILTER=official:codegen:arrays,official:codegen:structs-and-fields,official:codegen:nested-containers,official:codegen:aggregate-arguments-and-reference-fields` 全绿。
**常见坑**：数组参数退化成指针；字段按布局顺序而不是源码顺序求值；重复数组把 `e` 求值 N 次；结构体复制只复制了部分字段。

---

## I6 引用、place 与临时量

**为什么做**：Rx 的引用就是地址；place/value 区分决定了“取地址”还是“读值”。临时量具现化是最容易重复求值、也是 `evaluation-order` 用例的重点。

1. **引用表示**
   - 做法：`&T`/`&mut T` 都是 `ptr`；`&place` 返回 place 的地址；`*p` 作为 place 返回 `p` 本身，作为 value 再 `load`；`&&x` 是“指向指针的指针”。
   - 作用：`references-and-mutability`、`scalar-reference-operators`。

2. **place lowering**
   - 做法：写一个 `lowerPlace(Expr*) -> Address`：变量→`alloca` 地址；字段/索引→GEP；解引用→load 指针；括号→递归。`lowerValue` 若表达式是 place 就 `load`，否则直接求值。
   - 作用：赋值、借用、方法 receiver、字段/索引访问共用一套逻辑。

3. **临时量**
   - 做法：当字段/索引的基址、借用目标、方法 receiver 是值表达式时，先在入口块开一个临时 `alloca`，把值存进去，再对它取地址；该表达式只求值一次。
   - 作用：`make_array()[0] = 7`、`Vec::<i32>::new().push(5)`、`make().field = v`、`let r = &mut make_array()[0]`。
   - 注意：临时槽可以按语法位置静态分配；循环里复用同一个槽是安全的（逃逸到下一轮的引用属于生命周期违规，测试排除）。

4. **Box 解引用**
   - 做法：`Box<T>` 的值是 `ptr`；`*b` 直接把这个指针当地址；owned Box 的读取按 Copy/移动语义处理（IR 层都是 load/store）。
   - 作用：`box-and-moves`。

5. **Vec 索引**
   - 做法：从 Vec 结构里 load `data` 指针，再按 `index * size(T)` GEP；可变访问由语义层保证，IR 只负责用可变指针 store。
   - 作用：`vec-index-mutability`。

6. **可变引用重借**
   - 做法：`&mut *p` 就是复制 `p` 的指针值；不需要运行时动作。
   - 作用：`reference-coercions`、`reference-lub`。

**验收**：`make test STAGE=ir FILTER=official:codegen:references-and-mutability,official:codegen:reference-coercions,official:codegen:reference-lub,official:codegen:scalar-reference-operators,official:codegen:box-and-moves,official:codegen:vec-index-mutability,official:codegen:lifetimes-and-use` 全绿。
**常见坑**：取地址前先 load 了一遍；基表达式求值两次；把 `&mut T` 当值而不是指针；临时量生命周期只到语句中间导致引用悬空。

---

## I7 Box、Vec 与 derive clone/eq

**为什么做**：容器是综合用例的核心，也是 IR 阶段最容易出现“布局/步长/ABI 不一致”的地方。先把 IR 运行时写对，后面阶段照同一套布局重写。

1. **Vec 布局**
   - 做法：建议 `{ ptr, i32 len, i32 cap }`（顺序可自定），字段偏移写进 `docs/DESIGN.md`；空 Vec 的 data 可以为 null，len=cap=0。
   - 作用：所有 Vec 方法、clone/eq 共用同一布局。
   - 注意：容量不可观察；增长因子自定，测试只关心行为。

2. **`Box`**
   - 做法：`Box::<T>::new(v)` → `__rx_alloc(size(T), align(T))`，把 `v` store 进去，返回指针；`Box` 的 clone/eq 递归到 `T`。
   - 作用：`box-and-moves`、`copy-clone-and-equality`。

3. **Vec 基础方法**
   - 做法：
     - `new`：data=null，len=0，cap=0。
     - `len/is_empty`：load 字段。
     - `push`：满时分配新缓冲（如 0→4，之后翻倍），复制旧元素，更新 data/cap；在 `data + len*size(T)` store；len++。
     - `remove`：把元素 load 到临时，`memmove` 后续元素左移，len--；保持顺序。
     - `index`：`data + i*size(T)`，GEP。
   - 作用：`vec-operations`、`vec-index-mutability`。
   - 注意：`remove` 可能重叠，必须 `memmove` 而不是 `memcpy`；不要求缩容。

4. **递归 clone/eq**
   - 做法：按语义层 `supportsClone/PartialEq/Eq` 和 `getDerivedClone` 生成函数；结构体逐字段、数组逐元素、Box/Vec 递归；共享引用只复制指针，不克隆目标；引用相等比较地址。
   - 作用：`copy-clone-and-equality`、`recursive-traits`、`trait-dispatch-and-reference-equality`。
   - 注意：只生成被引用的 derive 函数，避免无界递归和重复符号。

5. **内存复制**
   - 做法：小而定长的复制直接 load/store；大小不定或较大时用 `memcpy`/`memmove`。可以声明 `@memcpy(ptr, ptr, i32)` 调 REIMU libc，也可以发 `llvm.memcpy` intrinsic。
   - 作用：Vec 扩容、remove、聚合复制。
   - 注意：可能重叠的范围一律 `memmove`；`memcpy` 只用于已知不相交。

6. **不释放**
   - 做法：不做 free/析构；旧缓冲区可以泄漏到程序结束。
   - 作用：规范明确允许；省掉大量生命周期代码。

**验收**：`make test STAGE=ir FILTER=official:codegen:box-and-moves,official:codegen:vec-operations,official:codegen:vec-index-mutability,official:codegen:copy-clone-and-equality,official:codegen:recursive-traits,official:codegen:nested-containers` 全绿；再跑 5 个 `comprehensive-*`。
**常见坑**：Vec 字段顺序在 IR 与后续阶段不一致；元素步长用成 1；`remove` 用 `memcpy`；clone 把引用目标也深拷贝了；eq 比较了 padding。

---

## I8 IR 收尾与验收

**为什么做**：IR 是第 8 周的独立验收点，必须 60/60，而不是“大部分能跑”。

1. **全量回归**
   - 做法：`make test STAGE=ir`；按目录归纳失败，优先修“一个规则挂很多用例”的问题。
   - 作用：60 个用例、115 组 I/O 一次跑完。

2. **IR 合法性自检**
   - 做法：对失败用例单独跑
     ```sh
     ./build/rxc --stage ir foo.rx -o /tmp/foo.ll
     clang-22 -S -x ir /tmp/foo.ll -o /tmp/foo.s \
       --target=riscv32-unknown-none-elf -march=rv32im -mabi=ilp32 -O0
     ```
     若安装了 LLVM 22 工具，可再用 `opt-22 -passes=verify -disable-output /tmp/foo.ll`。
   - 作用：把“IR 不合法”和“运行时行为不对”分开定位。

3. **把 `comprehensive-*` 当成集成测试**
   - 做法：5 个大用例逐个用 `--dump-ir`/日志定位；它们通常是前面规则的组合，不要单独打补丁。
   - 作用：验证整体一致性。

4. **回归语义**
   - 做法：`make test FILTER=official:semantic STAGE=semantic` + `make test FILTER=custom STAGE=semantic`。
   - 作用：IR 阶段如果改了语义接口，必须证明没破坏已冻结行为。

5. **写 `docs/DESIGN.md` 的 IR 章节**
   - 做法：记录 IR 内存表示、triple/data layout、类型映射、入口 wrapper、临时量策略、Vec 布局、runtime 接口。
   - 作用：后续阶段照此实现；Code Review 直接讲。

6. **打 tag**
   - 做法：`git tag ir-ok`；只提交具体文件。
   - 作用：第 8 周验收基线。

**验收**：`make test STAGE=ir` 输出 **60 passed / 115 runtime checks**；`official:semantic` 236 + `custom` 122 仍全绿。
**常见坑**：只跑子集就宣布完成；把 clang 警告当通过；`.ll` 里残留调试输出。

---

## 阶段验收总表

| 项目 | 命令 | 通过标准 | tag |
|---|---|---|---|
| IR | `make test STAGE=ir` | 60 passed / 115 runtime checks | `ir-ok` |
| 语义回归 | `make test FILTER=official:semantic STAGE=semantic` | 236/236 | — |
| 自定义回归 | `make test FILTER=custom STAGE=semantic` | 122/122 | — |

## 常见坑总表

- 退出码必须是精确 0/1；崩溃、signal、超时都算失败。
- `{output}` 必须真的生成；退出 0 但没文件判失败。
- `{runtime}` 必须是普通文件、≤8MiB；`BUILD` 只跑一次、上限 300s。
- `clang-22` 不在 PATH、target 写错、`-march/-mabi` 不匹配。
- 块没有终止指令；`alloca` 放在循环块；`bool` 用 i32 存。
- 回绕运算加了 `nsw/nuw`；移位没掩码；有符号/无符号指令混用。
- 短路逻辑用 `and/or` 先算两边；`loop` 的 break 值丢失。
- 聚合参数退化成指针；字段按布局顺序而不是源码顺序求值。
- 取地址前先 load；基表达式求值两次；临时量生命周期不对。
- Vec 布局/元素步长在生成代码与 runtime 之间不一致；`remove` 用 `memcpy`。
- 只跑子集就宣布完成；`.ll` 里残留调试输出。

## 依赖顺序

I0 → I1 → I2 → I3 → I4 → I5/I6 可交错 → I7 → I8。

**卡壳时的求助方式**：说清“在做哪个 I、哪条规则、测试用例文件名、你的预期和实际输出（附 `.ll` 和日志路径）”，再发我或助教。
