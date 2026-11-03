# MiniAS 当前清理与所有权总览（2026-11-01）

> **2026-11-03 COMMON 固定宽度重定位及跨对象 CFI 正确性矩阵
> （本轮工作树，远端 exact-head 尚待确认）：** 单一
> `probe-common-relocation-widths-gnu.sh` 在六种实际 ELF profile
> （AMD64/i386、RV32/RV64、AArch64、LoongArch64）上统一跑
> 1/2/4/8-byte × `A-B` 跨 Section、`A-.` 跨 Section、`A` 绝对符号
> 共 72 个场景。**GNU 在对应目标 ABI 接受与拒绝的能力就是判据**：
> GNU 拒绝时 MiniAS 必须拒绝且无对象，GNU 接受时要求有效 ELF
> class/machine、归一化语义而非对象文件逐字节相同，并用独立前置
> GNU 对象通过 `ld -r` 重定位后再次检查。x86 本地两模式
> **24/24** 场景（14 接受/10 同拒绝）、14 组 `ld -r` PASS，
> 本地安装 `pyelftools` 后 24/24 严格语义也已 PASS，
> 四个交叉 GNU 工具缺失的部分不冒充本地 PASS。比较器现支持
> i386 六种常用 `SHT_REL` 宽度的隐式 addend 解码；同类型同符号但
> addend 被篡改的对象必须不等，未知型为 INCONCLUSIVE。
> Target 专项门禁另外核验 x86 PC8/PC16/ABS8/ABS16 和溢出、
> RV32/64 ADD8/SUB8 配对、AArch64 ABS16/PREL16 与 ABS8 拒绝；
> LoongArch 复用原始 Linux `.alternative` ADD8/SUB8/ADD32/SUB32
> 严格 GNU 证据。`probe-cfi-debug-frame-link-gnu.sh` 保留 x86
> ELF32/64 真正 GNU `ld -r` 双对象 CIE/FDE 图对照，并增加
> RV32/RV64/A64/Loong 的同一双对象交叉 GNU 链接验证；新的
> CI 设置 `REQUIRE_ALL_GNU=1`，缺交叉 GNU 不会自动绿。
> **本地证据不是远端最终验收**；待整批合并 SHA 后执行新的
> Target CI，若发现目标与 GNU 不同的合法重定位能力，应定点修正
> 所属 Target/MC，不通过模糊 ELF 字节比较或跳过来掩盖差异。

> **2026-11-03 Target ISA/ABI→Emission→ObjectWriter 整链收口（本轮
> 工作树，最终提交 SHA/远端 exact-head 仍待确认）：** `TargetOps.resolve_profile`
> 将四架构的 `-march/-mabi` 解析成不可变 `MiniAsTargetSpec`；Target
> 私有 `profile` 是 ISA/psABI 语义来源，`layout.address_size` 是目标
> 地址宽度来源，`object.elf.initial_flags` 只是单向输出投影。
> `init_target_state` 从私有 profile 建立运行时 ISA/ABI；源内
> `.option arch`/模式切换保存在 TargetContext，并在 SourceSite/Fragment
> 留下现场快照。MC Encode 和代码对齐只通过窄 Sink 记录 Target 自有
> `target_output_features`（例如用过压缩 RISC-V 指令），Emission
> 统一汇总这些事实，不准直接改 ELF `e_flags`。终结的
> `MiniAsObjectWriterRequest` 借用一个只读 ObjectModel、不可变
> TargetABI 视图、输出特征和独立格式策略；唯一格式路由
> `minias_write_object` 将输出特征经目标 ELF policy 映射成 ELF flags，
> 再调用 `minias_write_elf_object` 做磁盘投影/原子提交。
> **新增两项实际链接运行的回归：** 四个历史 C `assemble_file*`
> ABI 符号及两个 Invocation 入口、属性开关不改变 RV32 ELF/编码；
> 三组 Loong lp64s/f/d 与三组 RV profile 的 `initial_flags` 故意
> 篡改测试。旧 `riscv_init_target_state` 的 RV32E 状态确被该测试
> 检出从 1 误变 0；本轮修复版实际重编后两项均 PASS。
> 目前 **ObjectFormat 只有 ELF 一个枚举值和实现**；格式分派是
> 后续 COFF/Mach-O Writer 的接入点，不表示两种格式已经支持，
> 实施前还需定义各自 relocation/symbol/section 投影与验收集。
> 旧 GNU 严格矩阵和原始 Linux/QEMU 不能跨 SHA 继承，最终需
> 以同一发布 SHA 的 exact-head 运行记录封口。

> **2026-11-02 Target DataLayout × ObjectFormat 终结边界合批
> （`352537a1` ELF policy 批次之后；新 SHA 的远端结果另核）：**
> 已将 `TargetSpec.layout.address_size` 设为四目标六配置唯一的机器
> 地址宽度来源（i386/RV32=4，amd64/RV64/A64/Loong=8）；动态
> 指令模式与 RISC-V 源内 ISA 选项仍由 Target/Site 独立管理。
> `TargetSpec.object` 只保存格式选择和 ELF 专有 flags/machine/REL
> 策略；ELF Target 的固定回调表继续唯一保存在 Target 私有
> `MiniAsElfTargetPolicy`，Driver 只借用，不复制。CFI、DWARF、
> ctor 数组、Target ABI 快照统一消费 DataLayout；一个 ELF Writer
> 从该宽度推导 ELFCLASS32/64，未知格式/非法宽度 fail-closed。
> ObjectModel 的 Section/Symbol 原生实体声明移到唯一独立
> `minias_object_records.h`，终结只读头不再引入 Common Session。
> 本地 `-Werror`、A0 30/30、旧新四目标 22 例和六配置 GNU
> section/CFI/DWARF、五配置 GNU `.debug_frame` 均已通过；
> **真正 COFF/Mach-O writer 未实现，四架构原始 Linux/GNU ld/QEMU
> P0/P1 以新 SHA 实际运行的 opt-in 证据为准，不自动声称通过**。
> 详见 [本批 DataLayout/格式边界](target-datalayout-objectformat-closure-2026-11-02.md)
> 与 [exact-head Linux 验收矩阵](minias-exact-head-full-acceptance-2026-11-02.md)。

> **2026-11-02 ObjectModel/源实体格式语义整链迁移（本批待发布 SHA）：**
> 从 GNU `.section/.type/.globl` 输入→Common Context 保留的
> SectionID/SymbolID→Target/Emission/Layout 消费→终结 ObjectModel
> →ELF Writer 全链收回类型/属性/符号的**语义值**。
> 六种 SectionKind、九种 SectionFlags、COMDAT 选择、Symbol 的
> binding/kind/visibility 不再是 ELF 磁盘 SHT/SHF/GRP/STB/STT/STV
> 数字；只有 `elf_writer.c` 做完整且拒绝未知码的格式投影，旧宏
> 与直通字段删除。ObjectModel 名称查询改为稳定 SymbolID，删除
> 对 const Symbol 指针反算序号的路径；仍复用原唯一 Context 实体，
> 不造影子图。新增独立生产 Writer 格式投影/GNU 严格对照门禁，
> 与旧测试一起验证。**真正 COFF/Mach-O writer 和独立地址宽度、
> 完整 Linux/QEMU P1 仍未完成**。详见
> [格式语义边界记录](object-format-semantic-boundary-2026-11-02.md)。

> **2026-11-02 MC/Target→ELF ABI 策略整链收口（接
> `dc7ec7fb`，本批发布 SHA 待定）：** 从四架构 TargetSpec/ISA
> resolver 沿 MCExpr、MCLayout、`.reloc`、CFI 到 ObjectModel/ELF Writer
> 复核后，删除通用 TargetOps 中全部散列 ELF 机器号、Fixup 编码、
> 命名重定位、折叠及可见性字段；ELF 数字映射留在 Writer，
> 唯一 `MiniAsElfTargetPolicy` 移至专用头，四目标各自静态只读持有。
> Driver 在初始化时 fail-closed，并将同一指针直接借给 Writer，
> 不再逐字段复制第二套策略。新增 14 个负向、2 个正向编译门禁。
> 本地 `-Werror`、A0 30/30、Target 结构门禁以及四架构 26 例
> 差分（18 对象 byte-exact、8 同拒绝）PASS；远端 exact-head
> 需发布后核验。该批没有改变 GNU 编码，没有实现 COFF/Mach-O
> 或 RISC-V 链接放松。详见
> [ELF 策略边界记录](elf-target-policy-boundary-2026-11-02.md)。

> **2026-11-02 P06 SourceGraph/SourceContext 语义能力整链迁移
> （基于 `a8035f88`，本批待发布）：** Source 物理行缓存、条件栈、
> 宏/REPT/IRP、稳定 SiteID、强制分支重绑和跨布局重放，整个递归调用链
> 已统一传递 Source 自己的 `MiniAsSourceContext`。Common 在每次
> Session 构造时绑定五项窄语义回调（绝对表达式、Target 条件、定义
> 查询、数字标签重写、诊断）以及不可变词法策略；Source 不再持有
> `MiniAs *` 参数。删掉旧 `minias_source_session`、`source_target_*`
> 和直接定义符号查询宽桥接，不造第二份 SourceGraph/Fragment 表，
> 不改变 GNU 宏条件语义或 MCLayout 放松算法。`-Werror`、A0 30/30、
> 24 道 Target 结构门禁、四目标 26 例差分、GNU x86/RISC-V
> 条件换臂重放及 RV 压缩分支 2→4→8 成长均 PASS；新 SHA
> 的远端 exact-head CI 和完整 Linux 原始源码/QEMU 另行验收。

> **2026-11-02 ObjectModel 终结期引用/原子输出整批迁移（基于
> `5d47f4f7`，新提交以发布 SHA 为准）：** 完整复核 Driver→
> 终结期对象借用→ELF symbol/fixup planner→minielf 后，将最终
> Fragment Fixup 定位与边界检查、Section 名称/关联及 COMDAT
> 签名 SymbolID 解析，统一搬到 `object_model.c` 的短生命周期
> 只读引用计划；对象落盘事务搬到 `object_output.c`。ELF Writer
> 删除旧的直接定位/关联/临时文件实现，只翻译逻辑 IDs 到
> STT/SHN/SHT/REL(A) 并序列化；不会再建一份持久
> Section/Symbol/Fixup 图。新增独立事务 C 探针、编译期 const
> 门禁升级及 exact-head CI 检查。已通过 `-Werror`、A0 30/30、
> GNU ELF32/x86 COMDAT+`ld -r`、1,024 LINK_ORDER、同名
> SectionKey 与原子输出；远端及四架构旧新差分以本批最终结果
> 为准。仍保留源 Section 上的 ELF 格式标记，不能宣称已支持
> COFF/Mach-O。详见
> [ObjectModel 终结期记录](object-model-terminal-plan-2026-11-02.md)。

> **2026-11-02 P06 MCExpr/MCLayout/Lowering SymbolID 整链收口
> （基于 `e7d76ee9`，本批提交待发布）：** 沿表达式语法树→延迟 DATA→
> Fragment 锚点固定点→`.size/.set/.equ/.org`→终结 ELF 的真实消费者
> 一次清走跨模块 `MiniAsSymbol *` 借用和实体指针反算 ID。Context
> 保留唯一可写 SymbolID 表与别名 DAG，新增稳定 ID 查询和只含
> Fragment/size 锚点的栈上布局值投影；MCExpr、MCLayout、
> Lowering、EncodeResult 不再读整个 `as->symbols`，旧跨模块
> `find/intern Symbol*` 与 `symbol_ref_lookup` 入口彻底删除。Driver
> 只在 ELF 终结视图中借用原 const Symbol 表；它的重放、初始化、
> 销毁生命周期经核对无需另造会话对象。新增布局视图正反编译门禁；
> 本地 `-Werror`、A0 30/30、四目标 26 例（18 个 ELF 字节一致、
> 8 个同样拒绝）、23 道 CI 结构脚本、GNU 表达式/.size/CFI/
> DWARF/RISC-V ULEB 组合回归通过。本批远端 exact-head CI 与
> Linux 原始 `.s`→ld→QEMU 仍须在新 SHA 上单独验证。

> **2026-11-02 P04 ISA 版本/ABI 入口成组迁移（基于 `639b6ffa`，
> 本批最终提交待发布）：** 上批已完成不可变 `TargetSpec.object`。
> 此批沿 Target 私有解析、文件 ISA、指令编码许可与 Fragment 现场
> mapping 真实调用链，把 RISC-V 11 个既有扩展的默认规范版本
> 纳入同一份带版本的 `MiniAsRiscvISA`。`-march` 可显式拼写已验证
> 默认版本（如 `rv64i2p1m2p0_zbb1p0`），GNU 2.42 的 ISA
> 属性与本地隐式版本对象一致；现有 `.option arch,+v/+zbb`
> 接受等价 `+v1p0/+zbb1p0`，但不扩展未覆盖的编码能力。
> ELF file attribute 不随源局部选项升级，现场 mapping 仍从
> Fragment 快照计算。x86 同时修复 `-march=i386` 被错误地配成
> ELF64 SysV 的对称 ISA/ABI 验证。`-Werror`、A0 30/30（GNU
> 本地 oracle 启用）、22 组旧新差分（15 byte-exact、7 对拒绝）、
> 四目标 TargetSpec 门禁、RISC-V 版本正反矩阵、x86 GNU ELF32
> 均 PASS；**远端 exact-head、原始 Linux / QEMU 仍待本批发布核对**。
> 版本策略与剩余边界见
> [ISA 版本记录](target-isa-version-2026-11-02.md)。

> **2026-11-02 P06 SymbolID 消费者值查询批次（接在
> `5d47f4f7` TargetSpec/ISA 版本批次后）：** Context 在唯一 SymbolID 表上提供三种只读
> `MiniAsSymbolView` 值查询（ID、名称、已绑定 SymbolRef）及
> 显式 `intern_symbol_id`。CFI 和 DWARF 的整个 Section/地址/
> Fixup 消费链不再直接索引 `as->symbols`，也不保存可被新建
> Target mapping symbol 重分配而失效的 Symbol 记录指针；
> Emission 的合成锚点预检、Fixup 和后期 LEB 提交改用 ID
> 检查/创建，删除跨域 `symbol - as->symbols` 反算 ID；Driver 的
> parser SymbolRef 对比和 SourceGraph 定义检查以及 Target 的
> Encode/Relax 投影一起走 Context。未建立影子表，已存在的
> ELF 终结期 const 实体借用保持不变。增加六种拒绝编译、
> 一个正向编译和跨生产 TU 的结构门禁；本地 `-Werror`、
> A0 30/30、RV32/RV64 GNU mapping 12/12、四目标 26 例旧新
> 差分（18 对象 byte-exact、8 对同拒绝）、GNU x86 CFI/DWARF
> 专项通过。**MCExpr、MCLayout 的完整别名/锚点规划仍有真实
> 消费者，尚未强行统一为贫化值视图；后续按独立子图处理。**
> exact-head 远端及 Linux 原始源/QEMU 仍按最终 SHA 单独核对。

> **2026-11-02 ELF ObjectModel 只读终结视图（基于
> `b2f0bfbe`）：** Context/Emission 刚完成 Section 元数据与输出字节
> 的所有权分离后，Driver 现在只在 Target finalization 成功后构造
> 一份**栈上借用**的 `MiniAsElfObjectView`；ELF Writer 整条符号
> 生存性、COMDAT/LINK_ORDER、重定位和原子写盘调用链均不再接收
> `MiniAs *` 或直接调用 Emission/Context。它只读既有 SectionID、
> SymbolID、Fixup 和 CFI/DWARF 结果，通过有界回调查询
> Fragment 位置、Section 字节及源 CU 文件；Target ELF 策略只投影
> 实际消费的值/回调，不带 parser、encoder 或生命周期能力。
> 移除旧 `minias_write_elf32/64(MiniAs *,...)` 签名，不复制第二份
> Section/Symbol 表。新增 7 个编译期拒绝、一个正向测试及 CI 门禁。
> `-Werror`、A0 30/30、四目标 26 例（18 对象 byte-exact、8 对
> 同拒绝）、GNU COMDAT/LINK_ORDER/符号生存性本地通过；
> exact-head CI/完整 GNU Linux 原始 `.s` 待本批提交验证。

> **2026-11-02 P04 TargetSpec/对象 ABI 只读所有权批次（基于
> `b2f0bfbeb`）：** 四 Target 的 resolver/init、Common Driver、CFI、
> DWARF、Emission 与 ELF Writer 已统一消费分组的不可变
> `MiniAsTargetSpec.object`；删去 Session 的三份可写 ELF class、
> machine、REL/RELA 副本。Emission 的 `elf_flags` 仍是从配置初始值
> 派生的唯一增量输出，绝不回写 ISA/ABI 配置。保留既有 Target 私有
> 动态状态与 Fragment Site，不生成影子状态机。新增七个负向/一个
> 正向编译门禁和四架构六种对象配置、i386 REL/AMD64 RELA、RVC 标志
> 测试；本地 `-Werror`、A0 30/30、五个旧新对象字节差分均 PASS。
> GNU RISC-V cross/pyelftools/AArch64 GNU 或跨架构 QEMU 无本地通过
> 结论；新 SHA 的 exact-head CI 仍须单独核对。详细见
> [TargetSpec 所有权记录](target-spec-ownership-2026-11-02.md)。

> **2026-11-02 P06 Section 身份与输出字节整批分离（基于已发布
> `ebe366e8`）：** SectionID 和唯一 SectionKey 元数据向量归 Context，
> 全 Common 仅能获得 `const MiniAsSection *`；原 Section 记录中
> `data/size/capacity` 三个字段彻底删除。Emission 私有的
> `MiniAsEmittedSection[]` 只保存对应 SectionID 的输出字节、NOBITS
> 长度与容量；ELF Writer、CFI、DWARF、Driver 消费临时 const
> bytes 视图，不复制第二套 Section 元数据。MCLayout 的
> LoongArch deferred LEB payload 清零也迁到 Emission 有界补丁，
> 不再直写 Section 字节。新增八个负向/一个正向编译检查与
> 82-SectionID + NOBITS + LEB mask 运行时所有权测试；旧测试的
> 伪造可写 Section fixture 已换成真实 Emission 事务。
> 本地 A0 30/30、26 组四目标旧新对象差分、GNU CFI/DWARF、
> SectionKey/`.org`/Fixup 和上下游结构门禁均 PASS；
> 本批远端 exact-head、交叉 GNU/Linux 仍按新 SHA 独立确认。

> **2026-11-02 P06 Symbol 编译期只读边界（基于已发布
> `bc2a6bb3`）：** 上一批已统一 Symbol 语义写入，这一批连同
> 25 个生产翻译单元的读路径与所有创建路径一起迁移。唯一可写
> Symbol 向量现归 Context 私有 `symbol_records` 持有；共享
> `MiniAs.symbols` 只是同一块内存的 `const MiniAsSymbol *` 视图，
> 名字与 alias section base 也只读。不复制实体、不造缓存图。
> 旧 `minias_get_symbol(name,create)` 已删除，分成只读查找与
> 由 Context 实施的显式 intern，两者都只返回 const 借用。
> 负向编译门禁覆盖六种非法写入/私有访问/旧接口，另有正向查询；
> 全生产编译、四架构旧新对象差分及 GNU alias 大小回归通过。
> 待本批远端 exact-head CI 验证后再确认全目标成功。后续余项
> 为 Section 借用/Target 最后权限、ISA 现场配置与对象格式扩展，
> **不把只读 Symbol 记录等同于完全 opaque SymbolID 值视图。**

> **2026-11-02 P06 Symbol 语义所有权整批收口（基于已发布
> `0ccb65f8`）：** Common Lowering 中的九类 Symbol 定义/声明操作、
> MCLayout 的每轮 Fragment 锚点与最终大小提交，以及 CFI、DWARF、
> MCExpr、Emission 的合成符号/隐式外部引用，全部走 Context 的
> typed transition。Context 持有唯一 SymbolID 表和别名 DAG；
> 不再保留 Lowering 的别名绑定写入、Layout 的直接 Symbol 更新、
> 旧的跨模块别名刷新入口或独立 Section size 发布入口。
> 新的全生产 TU 所有权门禁和合成符号门禁纳入 Target 工作流。
> GNU 验证发现并覆盖分支松弛导致别名 `st_size` 过期的旧缺陷，
> `.size alias,0/11`、别名前后定义顺序以及动态大小锚已按
> GNU 精确对象对照。**在 `bc2a6bb3` 这一 SHA 上仍保留 Common
> 内部可写 `MiniAsSymbol` 记录与旧 `minias_get_symbol` 借用；
> 后续只读边界见上段，P04/P06 仍未整体结束。**
> 本批 `bc2a6bb3` 的 Target ISA ABI、x86/Loong GNU/Four-Target、
> MiniC Native Strict、MiniC Four-Architecture Runtime 均 SUCCESS。

> **2026-11-02 P04 + 现场 ISA/mapping 一次性实现批次（以本批提交
> SHA 为准）：** 复核四 Target callback、头文件包含和阶段投影后，
> P04 的宽 `MiniAs *` 能力泄露已无待迁移入口；不为完成形式另建
> Context 或影子图。RISC-V 复用同一个 Target 私有 extension inventory，
> 将 file ISA/ABI、push/pop mutable option 与 Fragment site snapshot
> 分离；PostEmit 按**源事件顺序**读取最终 MCLayout offset，整块替换
> 原本 Section offset 0 单 `$x` 的旧生产者，生成 GNU 对应的
> `$x<site ISA>` / `$x` / `$d`，保留 subsection 顺序、重复同名符号、
> RVC 和 zero-width data 过渡。Common 只提供 exact-name 终结期
> symbol sink；SourceGraph forced replay 同时校验 mode/provenance。
> GNU binutils 2.42 的 RV32/RV64、六个 scenario 共 **12 组严格
> mapping/节字节/file attribute 对照通过**，本地 A0 30/30 与
> P04 结构门禁通过。该结论限已支持的 `+zbb/+v` 和相关 profile；
> 任意版本化 `.option arch` 及远端 exact-head、Linux 原始源对象、
> 四架构真实链接/QEMU 仍需独立验收，不能直接写作整体完成。

> **2026-11-02 P06 Section 元数据归属后续：** 解析期
> Section 对齐、首次 `.section` 的 `sh_entsize` 与 GNU `.ident`
> 的 `.comment` 属性升级统一归 Common Context；MCLayout 计算
> 一次性临时大小快照，在 Subsection/Symbol/别名/`.size` 归一化
> 后向 Context 一次发布所有 Section 的 `layout_size`。Lowering
> 和 Layout 不再直接改这些 Section 属性，未引入持久影子表。
> 结构门禁已加入远端工作流。**最终发布提交 `0ccb65f8` 的
> Target ISA ABI、x86/LoongArch GNU/Four-Target、MiniC Native
> Strict 和 MiniC Four-Architecture Runtime 工作流均 SUCCESS。**
> 前一 SHA `356a9b2d` 曾遇到 MCExpr 枚举旧头文件路径检查，
> `0ccb65f8` 已将其更新为 Common 私有存在/Target 公共不存在的
> 双向断言，并通过同一远端流水线。

> **2026-11-02 P05 重放/Target 公共类型继续收口（基于已发布
> `7d562531`）：** SourceGraph 的 Site 校验、强制分支查找、跨完整
> 重解析的 rebind/capture 已统一消费 Common 按当前 Fragment ordinal
> 提供的 `MiniAsSourceReplayReader` 标量快照。Source 不再包含私有
> Fragment 头，不拿 `MiniAsStmt *`、可写 MCInst、操作数数组或
> `growth_plan` 的 Target 上下文；只检查站点/操作数形状，并由
> Common 转交唯一 MCLayout typed relax plan。不缓存第二份图，
> 不改变原有 fail-closed 重放。旧 Source 词法逆向回调和同批
> Common/Emission `static` 改造导致的两处过时 CI 结构匹配也已
> 随新所有权更新；GNU payload、重定位、NOBITS 功能检查保留。
> Target 公开头继续移除八个仅 Common 使用的类型，分别下沉到
> Fragment/Session/Lowering 私有域，并增加负向编译测试。
> 本批的完整本地与 exact-head CI 结论见下方历史台账新记录，
> 不与 `7d562531` 的旧流水线混同。

> **2026-11-02 P04/P05/P06 联合边界批次（基于 `37394c1d`）：**
> 四目标在完成 parse 构造后统一通过 `InstructionView` 内
> `MCInstView/OperandView` 的栈上值投影进行 Measure/Relax/Encode；
> retained MCInst 仅在 parse/销毁阶段可写，后续能力不泄露裸指针；
> Source 已把条件、宏、repeat、前缀标签、`.end` 的裁决集中到
> 一个 typed `LABEL/TEXT` event sink 之前，Common 不再反调 Source
> 或重复扫描标签；Subsection 的写游标和指令对齐过渡归 MCLayout，
> Driver 初始节对齐经 Context setter，原始无符号 `.reloc` 经
> Emission 专属 API；ELF 对 Fixup 折叠和 COMDAT 签名各规划一次。
> 表达式符号加数已改消费唯一 syntax grammar，同时保留原 suffix
> 结合规则而非贸然扩展 GNU 接受面。代码与本地回归属于这一
> **新批次**；远端 exact-head、完整 GNU/Linux/QEMU 必须在最终
> 推送 SHA 上另行核对。RISC-V 现场 ISA→mapping 的未闭环计划见
> [下一批设计](riscv-site-isa-mapping-plan-2026-11-02.md)。

> **2026-11-02 P04 Fragment/Target 权限再收口：** 四架构 Parse、
> source-site capture 和 executable ALIGN 回调已一次性迁移到
> 受限请求/值类型站点视图；`MiniAsStmt`、Fragment payload union
> 及六个 variant 定义移入唯一的 Common/SourceGraph 私有头文件。
> Target 公开 ABI 不再包含完整 Fragment 记录；Common 在解析失败时
> 也从请求回收可释放的 MCInst 输出。新增 10 个拒绝编译、2 个正向编译
> 及 x86 三种递归别名失败路径并纳入 CI。SourceGraph 重放仍借用
> Common 私有 Fragment，归 P05 继续解耦。见
> [本批边界记录](target-fragment-private-2026-11-02.md)。

> **2026-11-02 MCInst 阶段权限收口：** 四 Target 的 Measure、Relax、
> Encode 已由 `const MiniAsStmt *` 改为 `const MiniAsInstructionView *`。
> Common 从唯一已解析的 MCInst 在阶段调用栈上生成操作数值投影：
> 源字段、bound SymbolRef、ISA 内存和 Target plan 仅 const 借用，
> 不暴露容量/析构函数或可写嵌套指针，不缓存第二套指令图。
> 增加 8 种编译期拒绝、运行时投影/重新投影/容量契约和 CI 门禁。
> Parse 仍保留唯一构建权；Site capture、ALIGN 已由下一批收窄。
> 见 [MCInst 阶段只读记录](instruction-readonly-phases-2026-11-02.md)。

> **2026-11-02 Target 只读查询收口：** 在 Fragment union 的基础上，
> PreLayout/PostEmit 原本直接借用的 `MiniAsStmt[]`/`MiniAsSection[]`
> 改成不透明阶段句柄 + 当前所有权对象的值类型投影；Relax/Encode 和
> ELF Fixup policy 的 Symbol 裸指针改为统一 Context 投影。可变
> Section/Symbol 实体类型不再定义在 Target 公开头文件；所有四架构
> 同批迁移，新增负向编译与 Target CI 门禁。保留原 SymbolID、
> SourceGraph、ELF、Fixup 单一路径。见
> [只读视图记录](target-readonly-views-2026-11-02.md)。

> **2026-11-02 Fragment 负载类型化：** `MiniAsStmt` 现有七类
> `FragmentKind` 共用一个带判别类型的 payload union（六个负载臂）；
> Parser、四目标、SourceGraph、MCExpr、MCLayout、Emission 和失败销毁
> 已整批迁移，删除旧顶层 `mcinst/mcdata/org/align/variable/bytes`
> 平铺字段，无双表示。LoongArch pre-layout 与 SourceGraph 在读取
> MCInst 前强制判别种类。新增负向编译结构门禁并加入 Target workflow；
> 本地 `-Werror` A0 30/30、延迟移位四目标 ASan/UBSan 和专项结构/GNU
> 回归已通过；远端 exact-head CI 仍须对应推送后核对。
> 见 [Fragment payload 明细](fragment-payload-2026-11-02.md)。

> **2026-11-02 P06 最新所有权批次：** 在 Emission 事务已经不透明的
> 基础上，Context 的 Section/Symbol/name 哈希及容量、Lowering 的 GNU
> 临时表与布局控制标记、MCLayout 的 Subsection/index/AnchorGraph
> 整批迁入各自私有状态；CFI 只通过 Layout 的 const 查找读取，Layout
> 只通过 Lowering 的值类型计划读取。Section/Symbol/Stmt 语义记录
> 继续保留稳定共享 ID，避免第二套对象模型。每轮源构建和失败销毁都
> 由现有 Driver 调度。见 [P06 明细](common-owner-split-2026-11-02.md)。
> 本段记录实现范围；实际远端验收以该批推送后 exact-head CI 为准。

## 执行 TODO（2026-11-02，按依赖顺序推进）

本清单是后续工作的唯一进度入口；具体设计仍在
`architecture-discussion-decisions-2026-10-31.md`，每批代码与 exact-head
证据进入 `full-source-review-ledger-2026-10-29.md`。
完成一项必须同时关闭旧入口并证明原有行为，**移动代码行不算删重**。

- [x] **基准冻结**：Draft PR #667 保持单分支；`49fa189` 的四个
  核心远端工作流 SUCCESS，本地 A0 30/30，独立旧/新 22 组
  15 对象逐字节相同、7 对同拒绝。
- [x] **C0 纯清理封口**：全生产代码消费者复核未发现新的
  高置信 dead static 函数；根状态/索引容量/CFI/DWARF/重放均有
  真实读写者。四个公开 `minias_assemble_file*` 在仓库中仅有
  一条测试消费者，但外部 ABI 消费者未知，保留到兼容迁移契约
  明确后再删；不为净删数字强行改写不同语义。另新增
  `probe-minias-public-api-compat.sh` 真正 C 链接/调用回归并纳入
  Target CI，防止历史公开入口被“无仓内调用”误判删除；这是
  **保留兼容接口的门禁**，并非确认外部用户已迁移。
- [x] **P04 Target 权限硬隔离（源码接口/本地结构门禁）**：清点所有宽 `MiniAs *` 回调；
  建立 Target 可见头文件与窄阶段 Context；四架构同批迁移
  directive、common validation、init/destroy 和 CFI mapping；删除
  旧入口，让四 Target 不能再包含完整 Common 内部会话头；
  结构测试 + A0 + 目标定向 GNU/链接/松弛/CFI + 旧新对象比较。
  **已完成子项：** 宽回调与五个 ISA TU 头文件隔离、PreLayout/PostEmit
  只读投影、Relax/Encode 符号值快照、四架构 Measure/Relax/Encode
  `MCInstView/OperandView` 栈上深层 const 投影；Parse、Site capture、
  ALIGN 的私有 Fragment 裸借用已由受限 Request/View 代替，Fragment
  唯一定义收回 Common 私有头，Common-only 的 MCExpr/Org/Deferred、
  literal、PendingLocalCommon 和 interned-name 类型也已从 Target API
  收回，增加编译期边界门禁。经最终代码审计，四 Target 所有
  `MiniAsTargetOps` 回调均为受限阶段 API，不再 include Common
  会话私有根头。**本地接口关闭，综合验收另计：** 本批 exact-head
  CI 与跨架构 GNU/链接/QEMU 尚未在最终 SHA 上全部运行；不把
  权限接口收口与整个工程整体验收混同。
- [x] **P05 Source/Common statement 边界**：旧
  `minias_statement_process(MiniAs *,...)` 已在先前批次删除；本批
  连同 Source→Common 的反向 conditional/macro/control 回调、
  Common 重复标签 lexer 一起删除。唯一受限 `LABEL/TEXT` sink
  保留 SourceSite、EOF、条件拓扑、强制增长和必要的完整
  SourceGraph replay。强制分支 Site 形状校验、rebind/capture 对
  原始 Common Fragment 的借用已替换为只读标量 Reader，无双图。
  **剩余独立工作：** 动态条件依赖读集/增量 SourceGraph（不能无
  证明删除正确性兜底）、本批 exact-head CI。
- [ ] **Common 职责拆分**：在 P04/P05 接口稳定后，把
  statement/directive、Section/Symbol、Fragment emit/commit、driver
  按消费者搬迁；Context/Lowering/Layout/Emission 已各有独立状态，
  本批进一步关闭可写 Subsection、raw Fixup/pattern 和 bootstrap
  alignment 外泄。**本轮已完成 Symbol 语义写入集中到 Context：**
  前端、Layout、CFI/DWARF、MCExpr、Emission 不再直接修改 Symbol；
  **Symbol 唯一可写数组已收归 Context 私有，外部 const 借用
  及查找/intern 分离均完成；本批又将 Section 元数据与 Emission
  字节缓冲按 SectionID 分离，关闭全部跨域 Section 可写指针和
  Layout 的直接 LEB 字节补丁。**此批收口 CFI、DWARF、Emission、
  Driver Source/Parse 和 Target Encode/Relax 的 SymbolID 值查询，
  仍需继续审计 MCExpr/Layout 的完整别名查询及 Driver 内部调度；
  Target 生命周期窄回调接口已通过 P04 审计，不重复造一层包装。**
  ELF 整条序列化调用链对宽 Session 的读取也已关闭，不再重复上述
  所有权工作。
- [x] **Fragment 负载类型化**：按 FragmentKind 收窄
  `MiniAsStmt` 的 DATA/ALIGN/ORG/INSTRUCTION 等专属 payload；
  Parser/Layout/Emitter/Target/SourceGraph 整批迁移，删除旧字段，
  保留原 Fixup、SymbolID、NOBITS 与分支松弛单一表示；
  exact-head 远端工作流仍须独立验证。
- [ ] **TargetSpec/ISA/ABI**：将不可变配置、源文件可变 ISA
  状态和每指令快照分开；以真实 GNU/Linux 输入驱动版本化
  扩展和 relaxation，不把 MiniC 的主动发射子集当 ISA 上限。
  **阶段结果：** TargetSpec/ELF ABI 已单所有者化；RISC-V 现有
  11 扩展的已验证默认版本、`+v1p0/+zbb1p0` 词法别名已由
  Target 私有 ISA 值驱动，四架构配置验证保留统一入口；
  其他 `.option arch`、非默认版本/relaxation 与 Linux 验收未完成。
- [ ] **ObjectModel/多格式预备**：只有前述所有权稳定且存在
  真实新格式验收时，才将通用 Section/Symbol/Fixup 与 ELF
  私有序列化拆开；**本批先完成 ObjectModel→ELF 写器的只读
  终结期边界，后续又将 Fragment Fixup 位置、Section 逻辑引用
  和原子输出迁到共用 ObjectModel 事务**，仍非 COFF/Mach-O
  写器。**本批又将 Common 的 Section/Symbol 持久 ELF 数字字段
  迁移为独立语义编码，ELF Writer 独占磁盘投影，ObjectModel 逻辑
  符号查询使用受检 SymbolID。**余下是格式专属符号/重定位策略、
  ABI 地址布局分层以及真正新格式后端；不预建空的 COFF/Mach-O
  全框架。
- [ ] **整体验收**：阶段性合并后对同一 SHA 完成多架构
  原始 GNU `.s` → MiniAS `.o` → GNU ld → QEMU P0/P1；严格
  3,536 GNU 对照另行定位差异，不替代运行时证明。

执行规则：大批量、按文件范围并行、主代理负责接口/冲突/验证与
推送；每批记 SHA、生产 `+/-`、受影响文件和精确回归范围。
有真实正确性故障时可越级修复，但必须在此记录插队原因。

**核对基线 SHA：** `5d8d10408b4ecc6499835aa287028234e8aabdc7`
（`refactor/minias-target-isa-abi-core`，Draft PR #667）。
本文是重新检查当前生产源码目录和设计记录后整理出的状态快照；
下表数字是该历史基线的快照，而非每次后续提交的实时计数。
旧台账保留历史证据，**这里不复用旧“pending CI”状态**。

**2026-11-02 第 34 批并行清理（基于远端 `fcfa2d8`）：**
六个生产 C 文件同时收敛 MCExpr 重复二元运算和 DATA 小端写入、
x86/AArch64 的数值寄存器解析、RISC-V 属性 ULEB/状态映射与
LoongArch 特权 form 编号；Source 为普通行跳过不必要的
collector 复制/缓冲增长。生产代码 `+185/-237`，净删 **52 行**。
本地 `-Werror`、A0 30/30 与旧/新四架构 22 组输入差分
（15 份 ELF 逐字节相同、7 组同时拒绝）通过。
新增可复用的手动旧/新对象比较脚本。远端新 SHA 的 CI、
完整 3,536 GNU 和 Linux Image/QEMU 与本地回归是不同验收，
不得混写；见[完整源码审查台账](full-source-review-ledger-2026-10-29.md)。

**2026-11-02 Source quote/escape 与 CFI 状态清理
（基于远端 `c9b2904`）：** SourceGraph 的 marker/物理注释
及 SourceExpansion 的行注释/宏分隔/分号分隔，共五处原本
重复的 quote/escape 状态机改为唯一 Source 私有函数，
保留消费者自己的不同 GNU 词法规则；CFI writer 删除死赋值，
并从唯一 CIE 哈希槽确定 variant index。四个生产文件
`+46/-78`（净 `-32`），GNU 词法结构与字节回归 `+25`。
独立旧/新 22 组四架构对象/拒绝行为无差异，本地 A0 和
CFI GNU 对照通过；新 exact-head CI/完整 Linux 另核。

**2026-11-02 联合 Source/Section/Target 清理（基于远端
`b401be5`）：** 宏体的三套平行数组换成唯一
`MiniAsSourceLine[]`，宏与 repeat collector 共用参数复制，
`.fill/.space` 的写入与 NOBITS 规则共用一条 Section
materializer；x86 GPR 四宽度词表及 XMM 数字解析、
LoongArch 数字寄存器与 `la.local/.global` 的 Fixup/RELAX
生产器也各自归并。同批 5 个生产文件 `+149/-192`
（净 `-43`），2 个结构测试 `+14`；本地 A0、x86 GNU
及四架构 22 组旧/新对象与拒绝行为差分通过。
**本批 exact-head CI/严格 GNU/Linux Image 仍须独立核对。**
之前的宏扫描提交 `b401be5` 的四项关键 CI 已是 SUCCESS，
不能把此结论直接移植到新批次。

**后续 P04 更新（基于 `e11e7d2` 的编码器批次）：** 四架构
`encode(MiniAs *, Stmt *)` 已一起迁移至不透明的
`MiniAsEncodeContext` 和 12 项受限能力，18 条闭合编码调用链
不再持有完整 Common 会话；旧 Target 字节发射入口也已私有化。
本地 `-Werror`、A0 30/30、重点事务/GNU 探针与旧版/新版四架构
18 份对象逐字节比较均 PASS。该批的远端 exact-head CI
以及跨 GNU/完整 Linux Image 验收必须单独报告；明细见
[完整源码审查台账](full-source-review-ledger-2026-10-29.md)。

**后续 P05 更新（基于 `c85a6960` 的 Source 所有权批次）：**
`assembler.c` 已不再包含 Source 私有头文件，也不直接访问
`as->source->...` 或 `graph->...`；通过新建的
`source_graph_common.h` 使用有限的处理帧、SiteID、EOF、重放
和统计能力。新增一个头文件后，当前目录为 **33 个生产文件
（22 C、11 头文件）**；下表 32 文件仍是旧基线的历史数据。
本地 A0、SourceGraph 单次文件读取与完整重放、EOF/宏/条件
定向测试及四架构 18 份对象差分通过，新批 CI 必须单独核对。

**后续 P05 增长交接更新（基于 `6173720`）：**
SourceGraph 与 MCLayout 之间的 Fragment 增长列表已统一为
`MiniAsFragmentGrowthSet`；唯一的分支成员位图移入 MCLayout
私有的 AnchorGraph，删除 `MiniAs` 根结构里未读的容量字段。
保留 SourceSite/typed MCInst 形状的严格失配拒绝和必要的
SourceFrame 重放。此批本地 A0、2/4/8 字节 RV 松弛、
双架构重放及清理前后 18 份四架构 ELF 字节对照已通过；
远端 CI 不继承上一提交的结论。

**2026-11-02 P05 宏词法清理（基于 `a7c63be`）：**
原来三处处理引号、转义与括号深度的宏参数扫描循环已收敛为
唯一顶层分隔器，声明可选 COMMA/SPACE 或组合模式。
生产代码净删 69 行，GNU 宏/REPT/IRP、可选/空参数、
大型嵌套和四架构对象差分均已做本地回归；不改其他
SourceFrame、`.irp` 列表或 Target 语法。仍需该批 exact-head CI。

## 一、读、改、完成必须分开

| 指标 | 基线结果 | 正确含义 |
| --- | --- | --- |
| 当前源码目录 | **32 文件：22 C、10 头文件** | 实际目录规模；不含 CLI 和 `elf/` |
| 历史全源码审查 | **35/35** | 对旧基线的通读，不是这次逐行重新证明 |
| 本轮清理起点 `9f563db` 起 | **22 次提交、19/32 文件曾修改** | Git 变更覆盖，绝非 59% 清理完成 |
| 生产代码净变动 | **+732 / −947，净减 215 行** | Git 实际差异，净删不等于架构关闭 |
| 上述基线的核心 CI | MiniAS ISA/ABI、x86/Loong GNU、AArch64/QEMU、MiniC native/四架构：**SUCCESS** | 只对这个 SHA 成立 |
| 严格 3,536 项 GNU 语义及全部 Linux Image/QEMU | **当前 SHA 未重跑** | 禁止套用历史结果 |

旧台账的“29/32 修改覆盖”使用更早的 10 月起点，
与上表 19/32 **不是一个基准**；文件是否触及与职责是否收敛
也不是一个概念。

## 二、真实数据流及其所有者

```text
调用/配置：Invocation、TargetSpec、ISA/ABI
  -> SourceGraph：物理文件一次读取、SourceSiteID、重放身份
  -> Source Expansion：.macro/.rept/.irp、条件、词法展开
  -> Common Parse：Section/Symbol/Stmt、共享 MC 词法与表达式
  -> MC：typed MCInst/MCExpr，布局、松弛与 Fragment 地址
  -> Target：各 ISA 编码、FixupKind/重定位意图
  -> EncodeResult：活动 Fragment 的字节/Fixup/锚点事务提交
  -> ObjectModel -> ELF Writer -> minielf ET_REL
```

Common 管源码语义、数据模型与最终对象；Target 管具体 ISA/ABI、
指令宽度和机器码；ELF Writer 负责序列化，不重新解释源语法。
COFF/Mach-O 是后续格式扩展，不允许反向污染当前 MC 语义。

## 三、旧基线 32 文件清单：哪些要保留，哪里还有债

| 模块 | C 源文件 | 判断 |
| --- | --- | --- |
| Invocation/配置 | `invocation.c`、`target_registry.c`、`riscv_profile.c` | **保留**：通用注册与 RISC-V profile 不重复 |
| Source | `source_graph.c`、`source_expansion.c` | **P05 后续更新**：缓存、宏展开、EOF 与私有类型归 Source；Common 已切换到窄接口，但 SourceGraph 与 MCLayout 的增长/重放契约仍需核对 |
| Common/MC 基础 | `assembler.c`、`mc_lexer.c`、`mc_operand_semantics.c` | **保留单路径**：Section/Symbol/Fragment 生命周期由 Common 管；`assembler.c` 仍是大型聚合点 |
| 表达式/布局 | `expression.c`、`mc_expr.c`、`mc_layout.c` | **合理分工**：共享语法，但 wrapping、绝对、延迟重定位求值语义不同 |
| 编码/目标 | `mc_encode_result.c`、`riscv_target.c`、`riscv_encode.c`、`aarch64_target.c`、`x86_64_target.c`、`loongarch64_target.c` | **P04 后续更新**：输出事务唯一；四个宽权限 `encode` 已换成受限上下文，其余能力边界仍需持续审计 |
| CFI/DWARF | `cfi.c`、`cfi_writer.c`、`debug_line.c` | **保留**：CFI 语义状态与两个 DWARF 消费端不同，不能只因字节写法相似就合并 |
| 对象/输入 | `elf_writer.c`、`string_literal.c` | **保留**：最终序列化与字符串解析职责独立 |

十个头文件：`minias.h`、`minias_internal.h`、
`expression_grammar.h`、`source_graph_internal.h`、
`riscv_target.h`、`riscv_profile.h` 与四个架构专属
`*_fixup_kinds.h`。它们**已包含**在上面的 32 文件中。
本次做了目录/职责和关键消费者核对，不宣称重新逐行证明
全部 32 文件没有死代码。

## 四、限制现状：解除、保留、待证明

| 限制/接口 | 真实状态 | 后续处理 |
| --- | --- | --- |
| 源码物理行固定 64 KiB | 已改为按需增长读取 | **已解除**，留 GNU 长行回归 |
| `.macro/.irp/.rept` 收集器 1024/256 字节复制上限 | 已改为复用动态缓冲；嵌套分类只认指令种类 | **已解除**，保留大输入用例 |
| 宏参数数量与展开递归上限 | 仍存在真实资源/收敛保护 | 不擅自放开 |
| Target 指定 Fixup Section、锚点所属 Fragment、指令输出 Section | 已被活动 Fragment 归属替代 | 保持结构测试阻止旧 API 回来 |
| x86 `.code16/.code32` Allowlist | 仍存在且参与模式合法性 | 先 GNU 全模式等价验证再删 |
| RISC-V `.option arch,+zbb/+v` | **站点级受限支持已经存在**，支持 push/pop，保留 file-ISA/strict-I 语义 | P02/P07 仍部分完成，不能说整个 extension 系统完成 |
| 更多 `.option arch`、正向 `relax` | 仍 fail-closed | 未有重定位/ISA 证据前不静默放行 |
| 条件拓扑触发的完整 SourceGraph 重放 | 正确性兜底 | 不以“重复运行”为由直接删除 |
| Source 字符串及 NameId/SymbolID | 两种不同身份、真实诊断消费者 | 保留；不属于无用字段 |

## 五、开放问题按依赖排序，而不是按文件数量

**P04 — 编码主阻塞已解除，其他边界仍要审计。** Parse 已使用受限
上下文，Measure/Relax/PostEmit 也有阶段能力；Fixup、锚点、
机器字节写入均被活动 Fragment 限制。后续批次将四架构
`TargetOps.encode` 与内部 18 个调用者统一迁移到真正不透明的
`MiniAsEncodeContext`，没有暴露 `MiniAs *` 的回退入口。
后续重点是验证剩余 Target 生命周期/策略边界，不能直接推断
完整 P04、跨 GNU 或 Linux Image 已全量验收。

**P05 — Source/Fragment provenance。** SourceGraph 负责物理行、
SourceSite、EOF 和稳定重放；最新 Source 能力边界已消除
Common 对私有帧/条件栈/图索引的直接访问。SourceGraph
仍须与 MCLayout 交换被强制增长的分支身份；后续已用一份
typed 工作表代替散落的三参数，并把临时位图归入 MCLayout。
下一步继续验证契约的失配拒绝、Fragment 稳定身份、宏递归、条件拓扑和
EOF literal pool/local COMMON，不随意移除必要的全帧重放。

**P02/P07、X1 — 目标 ISA/模式合法性。** 已有局部
RISC-V Zbb/V 站点支持，不代表完整版本化扩展语义；
严格 Base-I 字节审计、词法现场扩展、ELF 属性必须分开。
x86 模式白名单不能仅凭“重复检查”描述就删掉。

**P09/P14 — Reloc/名称表达式。** GNU symbol-zero
`.reloc` 已支持；完整语法及少数目标专用名字/dot 绑定未封口。
只有真实 GNU 差异才驱动扩展，不新增第二套表达式解析器。

**P03/P08/P12/O1/W1 — 延后但有边界。** 版本化 ISA、
未来 object format/endianness、格式中立符号绑定、GNU
section unique/groups，以及 3×7 workflow 收敛各需独立
输入/链接/语义对照。不能靠删除限制代码宣称完成。

**有证据应保留的实现：** full-source replay、
不同求值模式、四个 Target 私有 ISA 编码、
CFI 与 DebugLine 不同语义消费端、ELF 最终 Writer。

## 六、下一批怎样算真正“清理完成”

1. P04：四个宽权限编码器签名与旧调用链已替换。继续对新受限
   Context、RV32/RV64、AArch64 真链接/QEMU、x86/LoongArch GNU
   和原子提交做 exact-head 验收，逐项关闭其余生命周期边界。
2. P05：只在 SourceSite/EOF/条件重放输入可保持身份时删旧路径，
   不能另起一套 SourceGraph。
3. 统一删除证据：清掉无消费者的声明/字段/包装；
   文档记录精确 SHA、生产代码增删、测试变更和遗留问题。
4. 最终正确性要独立核对 3,536 严格 GNU 语义和 Linux
   原始输入的完整 GNU 链接/QEMU 验收，不能把强制 CI
   和这两个 opt-in 验收混成一个 PASS。

保留**单分支、单 Draft PR #667**。不为了提升 19/32
文件触达比例而修改已经稳定的 `invocation.c`、
`target_registry.c` 或 `mc_lexer.c`。
