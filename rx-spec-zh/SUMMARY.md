# Rx 编译器规范

[引言](00-introduction/00.00-introduction.md)

- [语言范围与测试保证](01-undefined-behavior/01.00-undefined-behavior.md)

# 源语言

- [记法](02-notation/02.00-notation.md)
- [词法结构](03-lexical-structure/03.00-lexical-structure.md)
    - [输入格式](03-lexical-structure/03.01-input-format.md)
    - [关键字](03-lexical-structure/03.02-keywords.md)
    - [标识符](03-lexical-structure/03.03-identifiers.md)
    - [注释](03-lexical-structure/03.04-comments.md)
    - [空白](03-lexical-structure/03.05-whitespace.md)
    - [词法单元](03-lexical-structure/03.06-tokens.md)
- [解析器约定](04-grammar/04.00-grammar.md)
- [crate 与源文件](05-crates-and-source-files/05.00-crates-and-source-files.md)
- [名称与作用域](06-names/06.00-names.md)
    - [路径](06-names/06.01-paths.md)
- [项](07-items/07.00-items.md)
    - [use 声明](07-items/07.01-use-declarations.md)
    - [函数](07-items/07.02-functions.md)
    - [结构体](07-items/07.03-structs.md)
    - [常量项](07-items/07.04-constant-items.md)
    - [实现](07-items/07.05-implementations.md)
    - [关联项](07-items/07.06-associated-items.md)
    - [生命周期参数与约束](07-items/07.07-generics.md)
- [trait 与属性](08-traits-and-attributes/08.00-traits-and-attributes.md)
    - [内建 trait](08-traits-and-attributes/08.01-builtin-traits.md)
- [语句与绑定](09-statements/09.00-statements.md)
- [类型与类型推断](10-types/10.00-types.md)
    - [数组类型](10-types/10.01-array.md)
    - [引用类型](10-types/10.02-pointer.md)
    - [Box 与 Vec 类型](10-types/10.03-heap.md)
    - [单元类型](10-types/10.04-unit.md)
    - [never 类型](10-types/10.05-never.md)
- [表达式与求值](11-expressions/11.00-expressions.md)
    - [字面量表达式](11-expressions/11.01-literal-expr.md)
    - [路径表达式](11-expressions/11.02-path-expr.md)
    - [块表达式](11-expressions/11.03-block-expr.md)
    - [运算符表达式](11-expressions/11.04-operator-expr.md)
    - [分组表达式](11-expressions/11.05-grouped-expr.md)
    - [单元表达式](11-expressions/11.06-tuple-expr.md)
    - [数组与索引表达式](11-expressions/11.07-array-expr.md)
    - [结构体表达式](11-expressions/11.08-struct-expr.md)
    - [调用表达式](11-expressions/11.09-call-expr.md)
    - [方法调用表达式](11-expressions/11.10-method-call-expr.md)
    - [字段访问表达式](11-expressions/11.11-field-expr.md)
    - [循环表达式](11-expressions/11.12-loop-expr.md)
    - [if 表达式](11-expressions/11.13-if-expr.md)
    - [return 表达式](11-expressions/11.14-return-expr.md)
- [常量上下文](12-const_eval/12.00-const_eval.md)

# 值与执行

- [引用与存储期](13-references/13.00-references.md)
- [Box 与 Vec](14-heap/14.00-heap.md)
- [内建 I/O 与程序入口](15-builtin/15.00-builtin.md)
- [后端](16-backend/16.00-backend.md)

# 附录

- [文法摘要](17-grammar-summary/17.00-grammar-summary.md)
<!-- - [Publication validation](18-open-decisions/18.00-open-decisions.md) -->
