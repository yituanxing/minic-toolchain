# Core 类型入口与后续 Module 边界：P0 实施契约

状态：**历史 P0 设计契约；CoreType 与 ModuleIR 已进入实施阶段**。源码核对基线：
`refactor/minic-target-language-boundary` / `2f8b74f`（2026-11-02）。
本文只定义 Core-owned 类型所需的最低语义、所有权和验证条件；
下文“现状”均指 `2f8b74f` 的历史基线，不代表最新实现状态；
**不以本契约代替已经完成的实现及验收凭证，也不是让 Machine IR 提前开工的 RFC**。
具体批次及实际删改、`+/-` 行数、验证凭证记入
[现行架构收敛账本](minic-refactor-ledger.md)，Bug 状态仍以
[MC 问题台账](minic-issue-register.md)为准。

## 1. 现状与本次目标

当前 `core/core_ir.h` 直接包含 `frontend/type.h` 和 `frontend/token.h`；
Value、Instruction、Object、Function、Callee、CallSignature 仍携带 `MinicType`，
FIELD_ADDRESS 携带 `MinicRecordId`，间接 CALL 签名携带
`MinicFunctionTypeId`。这些 ID 属于一个 `MinicC0Program`，不是独立 Core 身份。
本次源文件清单为 343 个生产 C/H/def 文件；旧 C38 记录中的 342 个
属于当时的历史快照，不能误用为当前改造进度分母。

`compiler/compiler.c` 在 C0 解析、验证、cast normalization、必要的 inline
特化及引用最终化之后调用 `minic_prepare_core_function_set()`。这是既有的
**C → Core 类型与语义事实快照点**；不将 C 的声明/表达式合法性搬进 Core。
`cast_normalization.c` 会替换 Expression arena，快照不得持有指向原 C0
arena 的长期指针。现有 lowering 仍串行运行；不要假定 `function_body.c`
的 CALL 遍历器已实现完全无写、可并行的源对象读取。

本阶段的第一验收目标是：**Core 公共 schema、构建、验证和执行类型查询
不再依赖 C 类型与 C Program**。`src/core` 中保留暂时性的 C→Core lowering
适配器是允许的；物理目录整理必须等接口迁移完成以后进行。

## 2. CoreTypeContext 的身份和生命周期

选择 **每翻译单元一个 Core-owned `CoreTypeContext`**，与当前单独分配的
`MinicCoreFunction` 共用类型身份。第一阶段可由 `CoreFunctionSet` 持有
该 context，后续再由语言中立的 Core Module / Program 容器接管；
**不为得到共享类型表先实现整个 Module IR**。

1. `CoreTypeId` 只能在其所属 context 内解释；不能将 C `RecordId`、
   `ArrayTypeId`、`FunctionTypeId` 改名直接导出，也不能跨 context 裸比较数值。
2. 所有已提交的 Core 类型描述不可变。Context 的生命周期覆盖每一个
   持有其 TypeId 的 CoreFunction、CALL 描述及 Target consumer；
   类型池销毁在最后一个消费者之后。
3. 标量类型可按可执行表示驻留/复用；数组与函数签名使用明确的
   结构、参数调整及调用约定规则；具名 Record/Union 的必要**名义身份**
   不能因大小或字段形状相同而被错误折叠。
4. C 入口建立从 Program-local C 类型身份到 Core 类型身份的临时映射。
   递归 Record/Union 先登记稳定 Core ID，再补齐字段与布局、校验完成状态，
   以处理合法的指针递归而不暴露半构造的布局。
5. 类型转换与函数 lowering 必须有原子发布/失败销毁边界；消费者不得
   见到未完成的类型或半成品 CoreFunction。Standalone Core 测试可自行
   初始化一个独立 context，而无需伪造 `MinicC0Program`。

`frontend/ast.c` 允许兼容的数组/原型具有不同的 C descriptor ID；
`core_lower.c` 还处理 declaration 与 definition 的 pointer-to-array
兼容情况。C 兼容性先在入口解决，Core 比较规则随后以归一化类型
及明确的名义身份为准。尤其不能将
`target/common/core_call_view.c` 对间接函数类型的严格身份检查
随手改成“大小一样即可”或宽松结构比较。

## 3. C → Core 无损事实清单

| 事实 | 入口转换后的 Core 所有权 | 不应带进 Core 的内容 |
| --- | --- | --- |
| 标量与指针 | 固定位宽、signedness、浮点格式、指针表示与必要的目标数据模型事实 | C `char/int/long` rank、plain-char、typedef、C usual arithmetic conversions |
| Enum | 使用已决议的有效底层整数表示；相应扩展/截断语义可验证 | `MinicEnumId` 及 C enum 兼容判断 |
| Array | 元素类型、固定长度/零长度/不完整性区别、对象存储所需大小和对齐；VLA 的运行时边界另由操作表达 | C 声明/typedef 身份、旧 `Local.is_array` 实现细节 |
| Record/Union | 独立 Core 身份、完整性、总大小/对齐、递归字段类型及字节/位偏移、位域宽度、packed/显式对齐、零尺寸/柔性尾部相关事实 | 前端 `MinicRecord *`、源 RecordId、原始表达式指针 |
| 函数与 CALL | 返回/固定参数类型、调用约定、是否变参、直接与间接 CALL 的签名身份、隐式聚合返回语义 | C prototype 合法性和物理 ABI 寄存器/栈位置 |
| 内存与效果 | load/store/record 操作的 volatile、atomic order、已证明的地址来源/有效对齐、对象存储范围 | 泛化成每个 SSA 值都有 C const/volatile qualifier |
| ASM / 目标源形式 | 已验证的 typed machine form/variant、必要 operand/effect/provenance 证明；机器消费者二次严格核验 | 把 C GNU ASM 语法合法性或目标内建类型的准入权移给通用 Core |

此表的 Record 形状信息不是新的统一 ABI 分类器。`target/data_layout.c`
继续在 Source→Core 入口充当 C 类型和目标布局 oracle；
五目标 ABI 分类器仍决定 HFA/HVA、SysV/Win64、ILP32/LP64(D)、sret、
寄存器和栈位置。`target/common/lp64_aggregate.c`、`x86_64/abi.c`、
`aarch64/abi.c`、`riscv64/abi.c` 等会递归查看字段类别，
所以**仅保存 `(size, align)` 绝不足以替换当前 `MinicType`**。

`MinicSourceSpan` 与通用调用约定枚举可在迁移 `core_ir.h` 的首批中抽成
中立公共声明；不需要先把整个 C Frontend 物理搬离 `src/core/`。

## 4. 分清 Core IR、后续 Module 和 Machine IR

当前 `compiler.c` 将 C `MinicC0Program*` 和 Core 函数集合一起传给
`minic_backend_emit_assembly()`。Core Function 只描述函数内部执行；
全局对象仍是 `MinicGlobalObject` 及独立的 ELF/全局发射路径。

在 CoreType 完成后，**再**设计独立于 C 的 Module/Symbol/GlobalData/
Relocation *视图*，覆盖：

- 函数定义/声明/别名及 Linkage、visibility、section、weak、constructor，
  并保留 C 函数集合与 Core slot 的过渡身份关系；
- 已物化的全局数据字节、零填充、实际对象大小（包括柔性数组尾部）、
  对齐、readonly、TLS、静态对象 owner 与真正发射根。原始 C
  initializer/union selection 在 C→Module 入口交给现有 Global Emit
  遍历器处理，不搬进中立 Module；
- 对函数、对象、**局部 Statement Label** 的 relocation 与 addend；
  `target/common/global_emit.c` 目前确实支持 `.Luser_<id>` 目标；
- PIC/PIE 的局部/不可抢占/undefined weak 符号事实，供 GOT、PC-relative
  选择；文件级 ASM 和具备原始来源资格要求的特殊操作。

这是**编译单元级的内容和链接关系**，不是 Core Function 的第二套
Value/Instruction/CFG；不应把 ELF relocation 或 Section 决策硬塞进
每个 `CoreType`。后续目标是让机器阶段不再回查 `MinicC0Program`，
但 SourcePolicy 仍可以合法读取 C 类型来判断 C/GNU 源码资格。

`target/backend.h` 已有 Core→Target lowering→可选 Machine IR→Emitter
接缝。Machine IR 将来负责目标 opcode、物理/虚拟寄存器、frame index、
机器 clobber 和 machine pass，**不在本轮实现**；也不要求先完成跨块 SSA
或新增优化 pass。现有 specialization、reachability 和保守优化保持行为兼容。

## 5. 逐批执行与退出条件

1. **P0：** 固定本契约、当前五目标正/负例和已知失败；单独补
   `CoreFunction.is_variadic`，解决 RV32 callee frame 从 C 定义回查的
   小缺口。`MC-014/015` 的过对齐变参边界必须单独留证。
2. **P1：** 引入 Core-owned TypeContext/TypeId，从整数、浮点、指针
   一类类转换现有 Core IR（不得长期维护并列的 CoreIR v2）。
3. **P2：** 完成 Array/Record/Union、函数签名、Core verifier、
   ObjectStorage/CallView/ABI/Frame/Memory consumer 的迁移；转换到新事实后
   在**同一切片删除**对应旧 C 查询路径。
4. **P3：** 再收敛 Module/Symbol/GlobalData/Relocation、全局发射与
   后端 Program 入参。后移 C→Core adapter 的物理目录，勿以目录整齐代替
   真实依赖删除。
5. **P4（待真实消费者）：** 逐目标引入 Machine IR、机器级 pass 与优化。

每批完成必须记录：当前 SHA、真实 producer/consumer、删掉的旧路径、
生产/测试代码 `+/-` 行、受影响五目标的 `-S` 或 GNU/native 差分、
已运行与 `SKIP/NOT_TESTED` 的边界，并写入
[架构收敛账本](minic-refactor-ledger.md)。

当前可复用的基本门槛包括 `make check-core-ir-shapes`、
`make check-core-ir-shapes-source`、RV32 变参/packed ABI、RV64 ABI、
`make check-x86_64-sysv-odd-record-abi`、
`make check-aarch64-fp-pointer-record-abi`、Core reentry 与 GNU ASM typed
正反例。必须保留不支持 HFA/HVA、特殊 Record/变参、未批准 machine form
等原有 **fail-closed** 行为；不能把拒编范围变化误记成成功。

`MC-014/015/076`、`MC-064/072/085` 等已知 OPEN 与新回归分开记账；
源码 `-S` 通过**不等于**五目标 GNU AS/ELF/QEMU/Linux 全链路通过。
完整 Linux 运行要在有交叉 GNU 工具链的环境另行验收，不因当前缺工具
被伪记为 PASS；也不为每个小切片反复跑整套 Linux。
