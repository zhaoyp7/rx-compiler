# 自定义回归测试

```text
tests/custom/
├── semantic/       语义检查用例
└── codegen/        IR 和后端运行用例
```

`semantic/` 中的用例检查 accept/reject 退出码。`codegen/` 中的用例同时用于
IR 和自研后端：manifest 写 `"stage": "codegen"`，现有 runner 会自动为其
派生 `ir` 阶段用例，不需要在 manifest 中重复登记。

## 运行

```bash
# 全部自定义语义用例
make test FILTER=custom:semantic STAGE=semantic

# 自定义语义用例的一个主题
make test FILTER=custom:semantic:containers STAGE=semantic

# 全部运行用例：rxc 输出 IR，再经 clang-22 和 REIMU 验证
make test FILTER=custom:codegen STAGE=ir

# 单独检查某个运行主题
make test FILTER=custom:codegen:reference-coercions STAGE=ir
make test FILTER=custom:codegen:clone-receivers STAGE=ir
make test FILTER=custom:codegen:never STAGE=ir

# config.mk 接入自研 CODEGEN 命令后，复用相同输入输出
make test FILTER=custom:codegen STAGE=codegen
```

runner 按目录递归发现 manifest。`FILTER=custom STAGE=semantic` 也可以选中全部
自定义语义用例。

## 覆盖范围

| 运行主题 | 内容 |
| --- | --- |
| `reference-coercions` | 多层 Box；共享/可变引用；let、赋值、实参、返回、字段、数组、分支、loop、容器构造 |
| `bool-abi` | 可变 bool 参数、方法、关联函数、递归、栈参数，以及聚合和堆内存中的 bool |
| `clone-receivers` | 引用层级、opaque/derive 结构体、Box 限定调用、共享引用复制、深克隆独立性 |
| `recursive-clone` | 递归及相互递归结构体，空/非空 Vec，Box，嵌套 Vec，数组和派生函数 |
| `never` | return/break/continue 出现在引用初始化、实参和容器操作中，及不可达副作用 |
| `evaluation-order` | 复合赋值运算、共享引用 RHS、索引变化、普通赋值、字段和 repeat 求值顺序 |
| `array-methods` | 数组在引用、Box、参数和临时量上的 len，以及接收者只求值一次 |
| `equality` | padding、bool、嵌套数组/Box/Vec、递归结构体、长度和不同位置的元素不等 |
| `containers` | remove 重叠移动、不同元素步长、移出拥有值、清空后复用、循环 push 和聚合 ABI |

输入文件覆盖 0、正数、负数条件，以及 remove 和相等比较中的首、中、尾元素。
无输入的用例使用 `"input": null`。所有运行用例都产生非空输出，以检测函数体
被错误替换为空实现的情况。

## 测试约定

`compilation_success` 和 `.out` 按语言规范填写。

使用宿主 Rust 交叉核对预期输出时，以下用例存在明确的语言差异，须以 Rx 规范为准：

```text
evaluation-order/acc-compound-shared-rhs-add.rx
evaluation-order/acc-compound-shared-rhs-shift.rx
evaluation-order/acc-compound-shared-rhs-xor.rx
```

Rx [运算符求值顺序](../../rx-spec-zh/11-expressions/11.04-operator-expr.md)
规定所有受支持的复合赋值都先求值 RHS，包括共享引用 RHS；Rust 对这些引用形式
可能采用运算符重载的 LHS 优先顺序。此处预期输出为先打印 RHS 标记 `1`，
再打印目标索引标记 `2`。

另外，[Never 规则](../../rx-spec-zh/10-types/10.05-never.md)允许 Never 实参转换到
Vec 元素的期望类型。因此 `never/acc-vec-push-return-control.rx` 应正常编译，
在插入元素之前提前返回。

添加用例时遵循 `tests/official/manifest.schema.json`，提供独立的源码和确定的
预期输出，避免无效借用、移动后使用、越界、零大小数据的可观察使用和不终止循环。
