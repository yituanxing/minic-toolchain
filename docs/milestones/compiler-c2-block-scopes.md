# C2 Block Scopes / C2 块作用域

## Status / 状态

MiniC now separates Program-owned local objects from Parser-owned lexical name bindings. Compound statements establish lexical scopes, inner declarations may shadow outer declarations, and leaving a scope restores the enclosing binding.

MiniC 现在已经把 Program 自有的局部对象与 Parser 自有的词法名字绑定分离。复合语句建立词法作用域，内层声明可以遮蔽外层声明，离开作用域后恢复外层绑定。

## Implemented boundary / 已实现边界

- one function-root scope containing parameters and ordinary locals / 每个函数具有包含参数和普通局部变量的根作用域；
- nested scopes for `if`, `else`, and `while` compound bodies / `if`、`else`、`while` 复合语句体的嵌套作用域；
- standalone compound statements / 独立复合语句；
- reverse active-binding lookup so the nearest visible declaration wins / 从活跃绑定末尾反向查找，最近可见声明优先；
- same-scope duplicate rejection / 同一作用域重复声明拒绝；
- out-of-scope use rejection / 离开作用域后的使用拒绝；
- Program-owned local slots remain stable after the name binding is popped / 名字绑定弹出后，Program 自有局部槽仍保持稳定。

Compound statements are currently flattened into the enclosing runtime Block. This is valid for the current subset because it has no variable-length arrays or other scope-exit runtime actions.

当前复合语句会平铺进外层运行时 Block。当前子集尚无变长数组或其他作用域退出运行时动作，因此这种降低方式是有效的。

## Differential evidence / 差分证据

| Program / 程序 | Coverage / 覆盖 | GCC/MiniC result / 结果 |
|---|---|---:|
| `block_scope.c` | nested control-flow scopes and three shadowed bindings / 控制流嵌套作用域与三层遮蔽 | exit 82 |
| `standalone_block.c` | nested standalone blocks and binding restoration / 嵌套独立块与绑定恢复 | exit 225 |

The Draft PR clean-checkout workflow passed:

Draft PR 干净检出工作流已经通过：

```text
Host fast gate       PASS
ASan / UBSan         PASS
Focused RV64 gate    PASS
GCC/MiniC matrix     PASS
```

## Next compiler boundary / 下一编译器边界

The next high-leverage path is pointer and array support. It begins with an explicit type/lvalue model rather than treating every expression as an untyped integer:

下一条高收益主线是指针与数组支持。起点是显式的类型和左值模型，而不是继续把所有表达式都当作无类型整数：

1. `int` and `int *` type records / `int` 与 `int *` 类型记录；
2. local object type and storage width / 局部对象类型与存储宽度；
3. address-of and dereference expressions / 取地址与解引用表达式；
4. dereference assignment / 解引用赋值；
5. fixed local arrays and indexing / 固定局部数组与下标；
6. array-driven real programs and then the first external small project / 数组真实程序，随后进入第一个外部小项目。

The external preprocessor, assembler, linker, and runtime boundary remains unchanged.

外部预处理器、汇编器、链接器和运行时边界保持不变。
