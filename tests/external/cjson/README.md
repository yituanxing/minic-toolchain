# cJSON external-project status / cJSON 外部项目状态

## Status / 状态

cJSON 1.7.19 is the active second real-project workload after the frozen tiny-AES AES-128 ECB milestone.

cJSON 1.7.19 是冻结 tiny-AES AES-128 ECB 里程碑之后的第二个活动真实项目。

The unchanged pinned core is used as a reproducible compiler-frontier workload. MiniC does not yet claim complete cJSON build or runtime acceptance.

未修改的固定核心用于建立可重复编译前沿；MiniC 当前仍不宣称已完整构建或运行 cJSON。

## Upstream identity / 上游身份

- Repository / 仓库：`DaveGamble/cJSON`
- Release / 版本：`1.7.19` (`v1.7.19`)
- Commit / 提交：`c859b25da02955fef659d658b8f324b5cde87be3`
- License / 许可证：MIT
- Vendored source / 入库源码：`tests/vendor/cjson/upstream/`

Pinned Git Blob identities / 固定 Git Blob：

| File | Git blob SHA-1 |
|---|---|
| `cJSON.c` | `6e4fb0dd369cd905923da515be87ab06db6c1ee0` |
| `cJSON.h` | `cab5feb427725f8e5c82287f7fe59481b609b9b5` |
| `LICENSE` | `78deb0406d713ab9730e3c2447be1abdbd70b9a2` |

## Current exact frontier / 当前精确前沿

The clean-checkout probe derives target-correct RV64 `size_t` from `__SIZE_TYPE__` and verifies the pinned source identities offline.

MiniC now crosses the previously recorded foundations in the unchanged core: native LONG semantics, self-referential tagged records, distinct plain `char`, `double` and `float` object types, function-pointer record fields, per-pointer-level `const`, and now **anonymous struct definitions bound through typedefs**.

MiniC 现已越过原生 LONG、自引用标签结构体、独立 plain `char`、`double`/`float` 对象类型、函数指针字段、逐级指针 `const`，以及当前新增的 **typedef 绑定匿名 struct 定义**。

The accepted anonymous record has a stable internal `record_id` but does not enter the record-tag namespace. This means:

```c
typedef struct {
    const unsigned char *json;
    size_t position;
} error;
```

is accepted, while the typedef name `error` does not make `struct error` a valid tag. Permanent focused tests cover both directions.

匿名记录具有稳定内部 `record_id`，但不会进入 record-tag namespace；因此上面的 `error` typedef 合法，而 `struct error` 仍不是有效 tag。永久正/负门禁同时锁定这两个方向。

The unchanged cJSON source then reaches:

```c
static error global_error = { NULL, 0 };
```

The exact first MiniC diagnostic is:

```text
cJSON.i:101:14: error: static global arrays currently require const integer elements
```

The parser currently restricts static globals to fixed arrays of const integer elements, so the active blocker is **a static record object with an aggregate initializer**. This is a distinct global-object/initializer capability and is not folded into the anonymous-record branch.

当前 Parser 仍把静态全局对象限制为 const 整数元素固定数组，因此真实下一缺口是 **带聚合初始化器的静态 record 对象**。它属于独立的全局对象/初始化语义，不并入匿名记录分支。

`tests/external/cjson/probe.sh` permanently anchors the float prototype at line 61, the anonymous record at line 97, and the static `global_error` declaration at line 101, and requires the exact line-101 diagnostic. Crossing it intentionally fails the gate until the next bounded branch records the following real source frontier.

`tests/external/cjson/probe.sh` 永久锚定第 61 行的 `float` 原型、第 97 行匿名记录以及第 101 行静态 `global_error` 声明，并要求精确的 line-101 诊断。后续越过该边界时，门禁会主动失败，直到下一条范围受限分支记录新的真实源码前沿。

## Validation ladder / 验证阶梯

The project advances through independently reviewable results: exact source identity, exact compiler frontiers, complete RV64 assembly generation, independent target linking/behavior comparison, and finally a frozen offline regression gate for the accepted cJSON configuration.

项目按可独立审查结果推进：精确源码身份、精确编译前沿、完整 RV64 汇编生成、独立目标链接/行为差分，最终冻结为已验收 cJSON 配置的离线回归门禁。

## Completion result / 完成标志

The cJSON milestone is complete only when the unchanged pinned core is compiled entirely by MiniC, linked with external target tools, and passes the declared behavior and reviewed project-owned tests against a GCC reference.

只有当未修改的固定核心全部由 MiniC 编译、由外部目标工具链接，并相对 GCC 参考通过已声明行为测试和经审查的项目自测时，cJSON 里程碑才算完成。
