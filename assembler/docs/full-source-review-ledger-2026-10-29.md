# MiniAS full production-source review ledger — 2026-10-29

> **最新状态导航（2026-11-01）**：先阅读
> [当前清理与所有权总览](current-cleanup-map-2026-11-01.md)。
> 本文保留带 SHA 的历史证据；下文旧条目出现的“等待 CI”、
> 1024/256 字节收集器上限、完全不支持 RV `.option arch,+zbb/+v`
> 和早期测试文件数量，均不能自动当作最新 HEAD 的事实。
> “读过 / 修改过 / 完成清理”必须分别统计。

### 2026-11-02 P04/P05/P06 + expression/ELF integrated ownership batch

**Exact pre-batch source:** `37394c1db82d8578bee3bad7aca07ac4475424c3`
on Draft PR #667, branch `refactor/minias-target-isa-abi-core`.
The independent previous-head assembler was rebuilt from a detached
worktree with `MODE=release CFLAGS=-Werror`; the candidate used the same
flags. This is a **post-parse ownership migration**, not a new ISA grammar
or a claim of complete strict GNU/Linux runtime conformance.

1. **Four-target P04 stage contract.** AArch64, LoongArch64, x86-64/i386
   and RISC-V now take a stack-local `const MiniAsInstructionView *` for
   Measure/Relax/Encode, projected from the current typed Fragment with
   Section/Subsection, current offset/size, source spelling/site and an
   immutable `MiniAsMCInstView` with copied scalar/const-borrowed active
   `MiniAsMCOperandView` slots. The existing retained MCInst is editable
   only during `parse_mcinst` construction; Measure/Relax/Encode cannot
   see its mutable operands, capacity or destructor pointers. The sole
   Common projector builds stack-local views; there is no second retained
   instruction graph. The newer, concurrently integrated `510d7931`
   projection supersedes the preliminary shallow `const MiniAsMCInst *`
   representation in this batch. Compile-time negative checks and the
   runtime view contract enforce the stage boundary.
2. **P05 one-way Source dispatch.** Source owns conditional active/inactive
   routing, macro/REPT/IRP/control invocation, and the sole leading-label
   colon lexer. The existing source-site sink is now typed `LABEL|TEXT`;
   Common exclusively defines a LABEL or lowers a TEXT, with the reverse
   `source_try_*` callbacks, duplicated Common label lexer and recursive
   label-tail call removed. The four old Source service declarations are
   now TU-private, not a public Common call-back door. SourceGraph cached
   physical reads, SiteID,
   conditional/forced-branch full semantic replay, EOF and macro recursion
   retain one original ownership and failure path.
3. **P06 Subsection/Context/Emission permissions.** Writable subsection
   creation, parse cursor, cursor advancement and post-DATA implicit code
   alignment state live only in MCLayout. Lowering gets typed touch,
   snapshot and state-transition services. Driver initializes a subsection
   through Layout and sets preparse `.text` alignment through Context's
   validated setter. Raw Fixup recorder and Section fill/pattern writers
   become Emission-only static implementations; the Common `.reloc` symbol-0
   case receives one explicit-section typed sink, preserving ELF
   `STN_UNDEF=0` and Fragment/Section Fixup origins.
4. **M1/M2 symbolic suffix de-duplication.** The old independent
   `parse_symbol_addend_inner` symbol/parenthesis scanner is deleted;
   symbol extraction consumes the shared syntax AST, with one wrapping
   integer helper. The original GNU-facing suffix contract deliberately
   remains `symbol + 4 << 2 == symbol + 16`, not a whole-symbol shift;
   `7+symbol`, `(symbol)+1`, second symbols and invalid shifts remain
   rejected. Checked absolute and deferred relocation policies remain
   separate consumers, not merged evaluators.
5. **ELF one-time object reference plan.** Each Fixup's Target
   section-symbol folding decision and each COMDAT signature SymbolID are
   resolved once for both symbol-liveness planning and final serialization.
   Mixed 2048 folded/2048 external relocation cases, empty Fixup lists,
   undefined local group signatures and AArch64 STT_FUNC folding veto have
   focused oracles. CFI and debug_line own distinct DWARF states; no
   COFF/Mach-O placeholder or parallel serializer was added.

**Original local candidate diff versus that baseline (before integrating
the concurrent remote commit):** **23 existing** `assembler/src`
files, **+618/-392** production lines, net **+226**. This is an ownership
and phase API change, so line growth is not described as a cleanup failure;
there is one persistent Fragment/MCInst/SourceGraph path. Test and workflow
changes are counted separately: nine previously tracked files
**+326/-12**, plus three new GNU Source/symbol-suffix test/fixture files
(65+116+39 lines). One focused RISC-V site ISA/mapping design document was
added, and the Source/Common contract and current TODO were updated.

**Independently re-run local verification after integrating all contributors:**

- `make -s -j8 MODE=release CFLAGS=-Werror check-minias-a0`: **PASS,
  30/30 objects**. Missing RISC-V GNU cross-as branches explicitly SKIP.
- `compare-target-readonly-baseline.sh` against exact `37394c1d` binary:
  **26 four-target fixtures, 18 byte-identical ELF objects, eight matched
  fail-closed rejections, zero mismatches**.
- Target readonly negative compilations and stack projection contract,
  hard header boundary,
  Common P06 ownership/transaction, typed Source sink negative boundary,
  GNU Source LABEL/TEXT dispatch, SourceContext macro/REPT/replay,
  four-target symbol suffix/GNU x86 bytes+three relocations, ELF reference
  planning and four-target EncodeResult with GNU explicit ABS0:
  **ten separate focused probes PASS** on the integrated tree.
- Domain owners additionally ran the x86 GNU growth/stress, RISC-V 2/4/8
  relaxation, SourceGraph full-replay/EOF/large collectors, GNU ELF
  COMDAT/CFI/DWARF, affine/signed-overflow/deferred-expression probes;
  those local results were **PASS**, not a replacement for strict full
  runtime proof. `git diff --check`: **PASS**.

**Not claimed:** remote CI for the **new** pushed SHA, missing local
RISC-V/AArch64/LoongArch GNU cross-assembler oracles, all 3,536 strict
GNU comparisons, original Linux full linked Image or QEMU P0/P1.
P02/P07 still needs a GNU-grounded effective site ISA → `$x` mapping
transition and SourceGraph replay identity; see
`riscv-site-isa-mapping-plan-2026-11-02.md`. Full P04 lifetime/ISA and
Common direct Section/Symbol mutator reduction remain open after this batch.
**Merged integration against that same `37394c1d` source base:** the
remote concurrent `510d7931` Target revision and local `433c6d75`
batch were reconciled by retaining the stronger value-type instruction
projection and the local P05/P06/Expression/ELF changes. Combined
production diff: **24 existing files, +729/-420 lines, net +309**.
The merged tree repeated Release `-Werror`, A0 **30/30**, four-target
old/new 26-case parity (**18 identical ELF + eight mutual rejections**),
both negative-compile Target view suites (**6 + 8**), runtime projection
contract (including zero-count and capacity checks), Source LABEL/TEXT,
Common fragment/Fixup, expression and ELF GNU oracles: **PASS locally**.
Source's four now-internal lexical services were removed from the public
Source/Common header in the same ownership family (production +4/-12).

**P02/P07 oracle preparation only:** added an opt-in GNU RISC-V RV32/RV64
lexical ISA mapping probe/fixture (nested `arch,+zbb/+v`, push/pop,
subsection and code/data transitions). It reports mapping-symbol
Section/offset/name/order differences as `EXPECTED_GAP`, not a pass,
and was **SKIP locally** because `riscv64-linux-gnu-as` is absent; it
is not a mandatory CI gate or an implementation of mapping transitions.
The 3,536 strict GNU, complete cross-architecture Linux and QEMU runtime
checks remain separate. Exact-head remote CI belongs to the final merged
commit, not either independently tested parent.

**Second concurrent Target boundary integrated:** remote `ef8ca2bc` builds
on `510d7931` and removes full writable `MiniAsStmt`/Fragment borrowing
from Parse, Site capture and executable ALIGN across the same four Targets.
The sole Fragment definition now lives in private
`minias_fragment_internal.h`; Target callbacks consume restricted request
and immutable site/alignment value projections. The merged Common path
preserves the local Source `LABEL/TEXT`, MCLayout/Emission owner changes.
Combined production scope versus `37394c1d`: **25 files,
+927/-567 lines, net +360**. Following this second integration, the
strict Release `-Werror` A0 **30/30**, 26-case byte/rejection parity
(**18 exact / eight matched reject**), 10-negative/2-positive Fragment
privacy gate, eight-negative operand-view gate, six-negative Target
projection gate, four-Target parse failure unwind and Source/GNU dispatch
probes all **PASS locally**. This proof is for the merged local tree;
exact-head remote CI and complete GNU/Linux/QEMU remain outstanding.

### 2026-11-02 P04 parse/site/alignment Target/Fragment private boundary

**Base:** Draft PR #667 remote head `510d7931292b361550d0c90de7d973d0aed23f7e`
(local identical-tree parent `e2fa007245ff4d18319d28f8f6bf6681b8fbd3ff`).
All five ISA target translation units, Common lowering/emission and the
shared internal headers were read against the live callbacks before edits.

- The old writable `MiniAsStmt *` parser callback became a narrow
  `MiniAsParseInstruction *` owning one MCInst output; Common publishes
  the output even on failure to retain its unique destructor path.
  x86 recursive lock/notrack/rep alias parsing copies only this request.
- The two remaining Fragment-wide callbacks now accept immutable
  source-site and alignment value projections. A canonical private
  Fragment union/record header is shared by Common and the SourceGraph
  replay borrow; no Target TU includes it, and no duplicate Fragment
  exists. The SourceGraph replay raw record dependency remains P05 debt.
- A new 10-negative/2-positive compiler contract joins required CI;
  three x86 recursive-alias rejection cases extend the failure-unwind
  probe. Existing Fragment/Target/readonly guards follow the moved owner.

**Production diff:** twelve C/header files, `+198/-147`, net **+51**.
The 77-line new private Fragment header replaces declarations removed from
the Target-visible header, not a second record graph. Overall pre-commit
scope is 22 files `+439/-166`, including required workflow, five tests and
four doc files. This is a coherent cross-ISA boundary migration, not a
claim that line additions are feature coverage.

**Local proof:** strict release `-Werror` A0 **30/30 PASS**;
Target/Fragment 10-negative/2-positive compiler contract, parser
failure-unwind nine cases, Target/Source/Fragment structural contracts,
four-target SymbolRef/EncodeResult/lowering and AArch64 finalization PASS.
An independently compiled pre-migration baseline matches **26/26** fixtures
across four targets: **18 byte-identical ELF objects and eight matching
rejections**. GNU x86 code16 **38/38**, i386 PIC real shared link,
SourceGraph macro/replay/conditional stable-site checks and x86 typed-relax
stress PASS. ASan/UBSan Target lowering, four-target SymbolRefs, parser
failures, AArch64 finalization and SourceGraph replay PASS with leak
detection disabled; LSan is unavailable in this runtime. Sanitizer build
uses its ordinary warning policy because an existing unrelated
`elf_writer.c` sign-conversion warning appears only with sanitizer flags.
RISC-V cross-GNU replay/stress cannot run locally without
`riscv64-linux-gnu-as` and remains a required remote-CI check. Exact-head
CI results belong only to the remote commit actually pushed; strict 3,536
GNU cases and Linux/QEMU remain separately outstanding.

### 2026-11-02 P04 Target directive/lifecycle/CFI capability and hard header split

**Base:** remote Draft PR #667 head
`49fa189472b3b72ff36518fc6c16b322794e2937`,
identical local source parent `28fbccd`. This is an ownership migration,
**not** a purported net-deletion batch or an expansion of supported ISA.

- Three Target directive implementations (RISC-V, x86, AArch64) and the
  RISC-V common-directive audit now take opaque `MiniAsDirectiveContext`
  rather than `MiniAs *`. Only TargetState mutation, current executable
  Section query, any-prior-instruction query, the existing absolute-DATA
  evaluator and a diagnostic sink remain visible. The RV privileged-spec
  check intentionally searches **all prior instruction fragments**.
- Four Target initializers now construct private states from immutable
  `MiniAsAssemblyConfig` and return them to Common, which alone publishes
  the state after success and destroys any partial failed allocation.
  Destructors take only `void *target_state`. RISC-V attribute bytes are
  validated before publication; x86 ELF32, AArch64 aliases and LoongArch
  mnemonic index failure paths retain their original diagnostics.
- AArch64's CFI data mapping requests use a separate opaque sink **at the
  original first EH FDE site**, not a post-emit callback. The sink determines
  the live Common symbol count and uses the original collision-safe symbol
  insert operation; $d/$x order and relocation identities are unchanged.
- One `minias_target_api.h` now owns all Target-visible typed MC, ABI and
  narrow capability declarations. The existing `minias_internal.h` owns
  the complete `MiniAs` session and mutable Common Section/Symbol/Fragment
  APIs. Four ISA Target TUs plus `riscv_encode.c` include only the former;
  no duplicate type definitions, full-session include or compatibility
  callback remains. An independent negative C compile verifies the session
  is incomplete to Target code. Existing structural gates follow their
  unique owners instead of checking the obsolete header location.

**Production scope:** 10 C/header files (one new Target API header),
`+1025/-917`, net **+108 lines**. Approximately 780 lines of existing
typed declarations were **moved** from the private header into their
unique Target-visible owner; this is not equivalent to 780 new functions.
Structural test adjustments: six existing scripts, `+71/-23`; docs separate.

**Local proof:** Release `-Werror` and A0 **30/30 PASS**;
Target compilation-negative hard-isolation guard PASS; RISC-V attribute
and ISA/ABI profile bytes, x86 GNU code16/i386 relocation, AArch64 aliases
and local CFI mapping, x86 CFI dual-frame/collision GNU probes PASS.
Independently compiled pre-batch MiniAS: 22 target/fixture pairs produce
**15 byte-identical ELF objects, seven mutual rejections, zero mismatches**.
Cross GNU AArch64/RISC-V/LoongArch tools unavailable locally; no claim of
complete 3,536 GNU corpus, Linux Image or QEMU from these local tests.
Exact-head remote CI must be verified on the newly pushed SHA.

**Still-open P04 hardening:** some borrowed `const MiniAsSection *` /
`const MiniAsSymbol *` views contain shallow mutable pointer fields.
Target can no longer see the Common session or call its mutators, but a
fully transitive immutable view needs another stage-specific DTO/accessor
slice with independent byte and fail-closed regression. Do not label this
remaining issue closed merely because the include boundary is now hard.

### 2026-11-02 four-lane MCExpr/Source/four-Target cleanup (batch 34)

**Base:** remote Draft PR #667 head
`fcfa2d8e59c21eb3f6558a6836c4f73ce5342b18`,
locally identical-tree parent `001f30e`. Four independent file scopes
were reconciled into one reviewed change; no public API, ISA allowlist,
fixup ownership, SourceSite identity or conditional replay contract changed.

- **MCExpr:** deferred binary nodes retain the shared
  `MiniAsExprBinaryOp` and reuse the checked absolute binary evaluator;
  section coefficients and cross-section rejection are still enforced by
  deferred evaluation. Removed the duplicate 18-way operation mapping,
  never-read node capacity, and five copies of low-endian fixed-DATA
  serialization. `mc_expr.c` `+41/-115`.
- **Source:** a conservative lexical substring prefilter skips collector
  argument-buffer growth and line copies for ordinary source lines.
  Inline semicolon lines allocate SourceFrame segments only if both repeat
  delimiters exist; the original full delimiter parser decides semantics.
  `source_expansion.c` `+47/-7`. A separate macro-counter arithmetic fix
  was explicitly left out because it changes legacy failure behavior.
- **x86/AArch64:** the four GPR widths and the AArch64 numbered register
  banks use one strict per-Target token lookup each, preserving x86
  AH/CH/DH/BH, AArch64 suffixes and register bounds. Two files `+38/-48`.
- **RISC-V/LoongArch:** a single RISC-V ULEB encoder computes size or
  writes bytes; one numeric attribute descriptor binds each of five names,
  ELF tags and existing state fields. LoongArch removes unused gaps in its
  private privileged-form IDs, retaining all nine encodings. Two files
  `+59/-67`.

**Production diff:** six pre-existing C files, `+185/-237`, net **-52**.
No production source or header was added or removed. Added a reusable
manual two-binary, four-architecture fixture parity probe (22 cases),
independent of A0 and GNU oracle routing.

**Local verification:** release `-Werror` and A0 **30/30 PASS**; targeted
GNU x86/Source and RISC-V/LoongArch/AArch64 available-host probes passed,
with missing cross GNU assemblers correctly marked SKIP. Independently
recompiled the exact pre-batch MiniAS: **15 ELF objects byte-identical,
7 inputs rejected on both sides, 0 mismatches** over 22 target/fixture
pairs. Remote exact-head CI is not inherited from the parent and must be
verified for the pushed commit. Full 3,536 strict GNU corpus and Linux
Image/QEMU were **not** rerun in this batch.

### 2026-11-02 Source 五处 quote/escape 状态与 CFI 槽状态清理（P05/CFI）

**Base:** exact remote Draft PR #667 head
`c9b29041e72a02b005c409c4496041f89a1a8c98`；
本地同树基准为 `3f971ca`。独立检查 Common/MC 的 SectionKey、
Subsection、SymbolID、别名解析和两套 Fixup staging 后，确认多数
相似检查有独立语义，未为凑删行数移除合法性证明。

- **唯一 Source 引号/转义状态：** 在 Source 私有头文件中提供
  `MiniAsSourceQuoteState`；SourceGraph 的私有符号标记检测、
  物理 GNU C 块注释剔除，以及 SourceExpansion 的行注释、
  宏顶层 COMMA/SPACE 分隔、分号语句分隔，共 **5 个消费者**
  复用相同 quote/escape 状态机。各消费者仍分别处理是否递归
  括号、是否终止物理注释、是否出现来源私有标记和 GNU 行分隔，
  没有引入另一条语义解析链。
- **CFI CIE 选择不保留幽灵状态：** `cfi_writer.c` 移除两处
  没有后续读取的 Section 指针赋值；CIE 哈希槽查找/插入在
  得到唯一槽位后读取 `variant_index`，不再依赖分支中不明显的
  初始化；EH/debug FDE 只读取一次对应 CIE offset。
  `eh_frame` 与 `debug_frame` 的指针和重定位规则保持不同，
  ELF Writer/DebugLine/字符串解析仍保留各自的必要职责。

**Production diff:** 4 个既有生产文件
`source_expansion.c`, `source_graph.c`, `source_graph_internal.h`,
`cfi_writer.c` 合计 `+46/-78`、净删 **32 行**；
一个已有 GNU 结构/功能测试 `+25/-0`。生产目录仍为 33 文件，
当前基准起实际修改覆盖仍为 22/33，不把覆盖数当完成率。

**Local verification:** Release `-Werror` 与 A0 `30/30` PASS；
SourceGraph 物理文件只读一次与跨重放 SiteID、宏/REPT/IRP、
70,004 字节长行、GNU 注释中带引号/转义/分号/块标记、
EOF、条件和嵌套展开的 GNU x86 对照 PASS；
CFI 初始/扩展状态、ELF32/64 `.debug_frame`、x86 CFI
register 和 vDSO `.eh_frame` 的本地 GNU 对照 PASS。
ASan/UBSan 宏生命周期检查 clean；LSan 在当前执行环境
不可用，不能声明泄漏检查已通过。
独立重编译修改前 MiniAS，四架构 **22 组**输入中
15 份 ELF 对象逐字节一致、7 组同样拒绝，差异 0。
跨目标 GNU binutils 缺失时明确标记 NOT_TESTED；
完整 3,536 GNU 与 Linux Image/QEMU 不由本轮 A0 代替。
新提交 exact-head CI 需在远端确认。

### 2026-11-02 Source/Section/Target 重复所有权联合清理（P04/P05）

**Base:** exact remote Draft PR #667 head
`b401be52dafa7cbbb1ebf93e16b6d276df54b1c1`；
本地同树清理父提交为 `9153913`。这次以五个生产文件为
**一批**，只合并已经验证属于同一职责的旧实现，未扩大目标
指令集、宏语言或 ELF 语义。

- **Source 宏体只保留一个拥有者：** `MiniAsMacro.body` 从
  文本、物理行号、源段号三套平行堆数组改成唯一
  `MiniAsSourceLine[]`；定义时一次分配，宏重定义/注销/失败清理
  复用 SourceFrame 原有逐行析构。扩展时保留每行的 source
  provenance、宏计数器、参数替换顺序与原有的快照生命周期。
- **收集器复制只保留一份：** `.macro` 与 `.rept/.irp` 的
  指令识别、前置标签和闭合语法仍各自独立；公共的空白跳过、
  动态参数复制、截断检测不再在两条路径实现两次。
- **Common 填充序列化只保留一条：** 一字节 `.space` 和
  多字节 `.fill` 的 Section 扩容、尺寸检查、NOBITS 非零拒绝
  与重复模式增量复制统一为 `minias_section_append_pattern`。
  Fragment staging/commit 仍是唯一源输出事务；NOBITS 的
  `.space` 忽略填充值规则不变。
- **Target 去掉两类重复词表与计划：** x86 四种宽度的 16 个
  通用 GPR 名称共用一份索引表，XMM 共用已有的受限向量数字
  解析器（AH/BH/CH/DH 仍独立）；LoongArch `$rN/$sN`
  复用已有的受限数字寄存器解析器，`la.local/.pcrel` 与
  `la.global` 两机器字、四 Fixup/RELAX 布局共用一个参数化
  生成器，仍以不同的 opcode/relocation/form 描述语义。

**Production diff:** 5 个既有 `assembler/src` 文件，
`+149/-192`，净删 **43 行**；两项结构测试 `+14/-0`；
生产文件目录仍为 **33**。此前修改覆盖 22 个文件，本批没有
为了增加覆盖率触及已稳定的其他模块。

**Local verification:** Release `CFLAGS=-Werror` PASS，A0
`30/30` PASS；GNU x86 源码宏、可选/空参数、复合重放、
大 collector、IRP 512 值、Fragment 原子提交/数据填充
与 NOBITS 等定向测试 PASS；Target 侧 x86
GPR/CFI/code16 及 LoongArch 别名/PCALA/GOT/RELAX、
  标量与寄存器库正反例 PASS。ASan/UBSan 宏生命周期检查
  clean，LSan 在本环境不可用（不能宣称 leak check PASS）。
  单独从清理父提交重编译旧版
MiniAS，四目标 22 组输入：**15 份 ELF 对象逐字节一致，
7 组两边均拒绝，状态或对象差异 0**。
本机 RISC-V/AArch64/LoongArch64 外部 GNU 交叉汇编器
不可用；严格 3,536 GNU 语义、完整 Linux Image/QEMU
与新远端 exact-head CI 未在本地宣称通过。

### 2026-11-02 GNU 宏参数顶层词法扫描统一（P05）

**Base:** exact remote Draft PR #667 head
`a7c63be2bc7feedc9bda0c705f79b7be98fcc292`。
这次找到的是确实重复、且语义可一一核对的旧实现：
`split_macro_tokens` 和逗号分隔的 `split_macro_arguments`
各自重复处理引号、反斜杠转义和 `()[]{}` 深度；
`find_macro_top_level_separator` 已实现同一完整语法。

- **删除两套重复扫描循环：** 唯一的
  `find_macro_top_level_separator` 使用分隔位 `COMMA/SPACE`
  或两者组合；GNU `.macro` 形参、普通空格形参、逗号调用
  和缺省参数的补位均复用它。引号内的逗号、括号内表达式、
  转义字符、显式尾随空参数与无逗号单参数算式保持原本判断。
- **没有新词法解析器或 Target 分支：** `split_macro_arguments`
  的空槽/关键字/vararg/表达式运算符规则原样保留，
  `.irp` 值列表和 `find_statement_separator` 的不同 GNU 语法
  仍由各自消费者负责。新结构守卫要求声明及调用共用唯一
  顶层扫描器，拒绝再次引入单独的 quote/depth 解析循环。

**Production diff:** 1 个既有 `assembler/src/source_expansion.c`，
`+40/-109`，净删 **69 行**；已有宏/REPT/IRP 结构测试
`+14/-0`。目录仍为 33 个生产文件。

**Local verification:** `-Werror` release、A0 `30/30`；
GNU x86 宏/可选参数/空参数/嵌套 collector、宏注销与失败清理、
大收集器、复合 SourceContext、条件/EOF、SourceSite 重放与
70,004 字节物理行定向测试均 PASS。另独立重新编译改动前 MiniAS，
四架构 18 份合法输入的 ELF 对象逐字节一致，5 份非法/不适配
输入两版均拒绝，行为状态差异 `0`。
本机 AArch64/RISC-V/LoongArch64 外部 GNU cross assembler
缺失，其外部逐字节差分 **NOT TESTED**；当前提交的远端
exact-head CI、严格 3,536 和完整 Linux Image/QEMU
仍须单独报告，不能套用旧提交结论。

### 2026-11-01 SourceGraph ↔ MCLayout 分支增长交接清理（P05）

**Base:** exact remote Draft PR #667 head
`6173720bea0ced85096ee74985e1c9268450be16`。
本批不更改跨重放 SourceSite 的强制分支语义，也不删掉
`.if/.set` 依赖的整帧重放；仅收回跨层传递的临时状态所有权。

- **一份 typed 增长工作表：** `MiniAsFragmentGrowthSet` 取代
  Common 调用者、SourceGraph rebind/capture 和 MCLayout solve/collect
  之间重复传递的 `ids/count/capacity` 三参数。
  SourceGraph 仍按稳定 SourceSite 与 MCInst operand shape 验证、
  拒绝缺失/重复/变形分支，再重绑为本次 pass 的 Fragment ID；
  MCLayout 原有唯一工作项和 RV C16→B32→long8 单调增长不变。
- **MCLayout 独占成员索引：** 分支位图移至
  `MiniAsAnchorGraph.branch_membership`，由同一 private graph
  分配和析构，通用 `MiniAs` 会话不再存放内部位图指针。
  同时删除原本只赋值和清零、没有任何读取者的
  `forced_long_branch_bitmap_size` 字段。第一次扫描先确保
  已有的统一 AnchorGraph 已创建；后续固定点复用它，不新增
  一套布局或 SymbolID 查找算法。
- **结构守卫：** MCLayout owner 探针验证工作表仅在 Common
  持有、SourceGraph 使用受限参数，以及 bitmap 不能再回到
  `MiniAs` 根结构。没有增加兼容旧入口或双通道。

**Production diff:** 5 个既有 `assembler/src` 文件，
`+56/-57` 行，净 `-1`；一个结构测试 `+11/-2`。
此前的 33 文件目录规模不变。净删除数字很小，但消除了
两个 session-wide 临时字段和重复三参数接口。

**Local verification:** `-Werror` release、A0 `30/30`、
RISC-V 2/4/8 字节两级松弛与源码重放、稳定 SiteID
重绑定/失配拒绝、x86 和 RISC-V 单次物理文件读取与
完整 `.if/.set` 重放，以及 x86 GNU byte/symbol oracle PASS。
独立以旧版源树编译 MiniAS，对四架构 18 份成功 ELF 对象
逐字节对照一致；另外 5 份两版均拒绝，没有状态分歧。
本机缺跨 GNU assembler 的 RISC-V/AArch64/LoongArch64
外部 GNU 差分 **NOT TESTED**，本批 exact-head CI、
严格 3,536 项 GNU 和完整 Linux Image/QEMU 须单独报告。

### 2026-11-01 SourceGraph/SourceContext 不透明 Common 边界（P05）

**Base:** exact remote Draft PR #667 head
`c85a6960d4ff1bb170d2cfbfe216e918716ab0f5`。
本批清理的是 Source 所有权的实际泄露，不删掉为保证 GNU
`.if/.set` 和长分支反馈正确性而保留的整帧语义重放。

- **Common 不再包含 Source 私有布局：** 新的
  `source_graph_common.h` 仅声明不透明 `MiniAsSourceGraph` /
  `MiniAsSourceContext` 的能力。`assembler.c` 不再包含
  `source_graph_internal.h`，且不再使用 `as->source->...`
  或 `graph->...` 读写条件栈、SiteID、FILE frame、图统计。
  详细 frame、macro、conditional 和稳定索引仅在 Source 的
  两个 `.c` 与私有头文件可见。
- **SourceGraph 唯一负责持久图生命周期和统计：** Source 层
  `create/destroy` 自行分配、释放图，判定物理文件是否已经加载，
  提供只读物理行数并原样输出重放 trace。Common 依旧只要求
  读取物理文件一次，失败与成功路径都释放持久图。
- **SourceContext 唯一处理 pass-local frame：** Source 层把
  持久 graph 绑定到新语义 pass，递增原有 `parse_passes`，通过
  同一 `minias_source_frame_process` 处理 FILE/扩展帧并检查
  `.if` 栈是否闭合。Common 仍独立处理 CFI 完结、EOF 的
  literal-pool/local `.comm` Fragment、layout 和 ELF 输出。
  SourceSite 获取/恢复仅供 Common 记录普通 Fragment 和恢复
  EOF 延迟 local `.comm` 的原始语法位置，不提供可变 Source
  栈或图索引指针。
- **守卫：** `probe-mc-layout-ownership.sh` 强制 Common 只能
  包含受限头文件，拒绝直接访问 Source 内部，确认展开/文件帧
  仍共用同一处理器；旧的源码条件/宏解析器没有重生。

**Production diff:** 5 个 `assembler/src` 文件（含新增 1 个
受限头文件），`+125/-62` 行，净 `+63`；结构测试
`+17/-4`。目录随之由 32 个生产源码/头文件变为 **33**
（22 `.c`、11 `.h`）；这是消除 Common 私有类型依赖，不是
净删代码或关闭全部 P05。

**Local verification:** `-Werror` release 与 A0 `30/30` PASS；
x86/RISC-V SourceGraph 单次物理读取、稳定 SourceSite、
完整 SourceFrame 重放/分支增长、x86 GNU 字节+符号对照、
EOF local `.comm`、复合宏/条件/`.end`、大型收集器、
70,004 字节物理行等定向回归 PASS。独立重编译迁移前
MiniAS 与新版本比较，四架构 **18 份 ELF 对象逐字节一致**，
其余 5 份输入两版均拒绝；无成功/失败状态差异。
RISC-V、AArch64、LoongArch64 本机外部 GNU 差分因缺少
cross assembler **NOT TESTED**。新提交 exact-head CI、
严格 3,536 GNU 和完整 Linux Image/QEMU 仍待独立报告。

### 2026-11-01 四架构完整编码回调的受限能力迁移（P04）

**Base:** exact remote Draft PR #667 head
`e11e7d28a3aab81766b86cac902e82dff3c935d5`。
本批一次性替换四架构 `TargetOps.encode(MiniAs *, Stmt *)` 的宽权限入口，
不改变 ISA 指令表、布局/松弛策略或 ELF 重定位算法。

- **Common 独占可变会话：** `MiniAsEncodeContext` 仅在
  `assembler.c` 定义、构造并持有 `MiniAs *`。Target 只见不透明声明，
  `encode` 和递归调用的 **18 个函数/辅助函数**全部接收该上下文；
  无 `ctx->as` 或另一条旧 `MiniAs *` 编码回调路径。
- **12 项窄能力：** 活动 Fragment 字节写入、符号 Fixup、ABS0 Fixup、
  锚点、ELF flags、错误报告、符号查询、两种符号差值求值、
  `. / SymbolID` 判断、只读 Target 私有状态、ELF32 类别判断。
  Common 仍负责 Fixup 顺序、符号/Section 归属、单次事务提交、
  诊断首错及实际 SymbolID 转换；Target 不获得 Section、SourceGraph
  或可变 Fragment 的裸指针。
- **移除旧的 Target 字节发射入口：** 原
  `minias_emit_instruction_bytes(MiniAs *,...)` 仅剩
  Common-private `minias_stage_instruction_bytes`，ALIGN sink 和受限
  Encode Context 共用现有字节事务。RISC-V 的即时 RVC/XLEN 状态、
  x86 i386 ELF32 判别、AArch64 对 `.` 的解析、LoongArch
  可松弛 Section 缓存仍读取原本的实时状态。
- **结构守卫：** 两份已有探针校验四架构签名、18 条闭合编码调用链、
  恰好 12 项能力、Common-private context/byte stage、旧入口消失。

**Production diff:** 7 个 `assembler/src` 文件，`+341/-249`
（净 `+92`）；两个结构测试 `+69/-20`（净 `+49`）。这是确实收紧
所有权的接口迁移，**不是净代码缩短**，也不意味着其他 P04/P05
任务或跨工具链全量验收已完成。

**Local verification:** `-Werror` release、A0 `30/30`、
Fragment/EncodeResult 原子事务、四架构 Target 边界、GNU x86/i386
字节/REL/GOTPC 真实链接、`.code16/.code32` `38/38`、
AArch64 `.` 分支真链接、RISC-V 2/4/8 字节松弛、
Linux x86 页表以及主机 CFI/COMDAT 探针 PASS。
**独立旧/新二进制差分：** 将迁移前源树重新编译成单独的 MiniAS，
18 份来自四架构的成功汇编 ELF `.o` 逐字节一致；另 5 份非法/不适配
输入两版均拒绝，没有不同的成功/失败状态。
本机缺少 AArch64/RISC-V/LoongArch GNU cross assembler，相关
外部 GNU 字节差分 **NOT TESTED**。本批 exact-head remote CI、
严格 3,536 GNU 及完整 Linux Image/QEMU 验收需另行报告。

### 2026-11-01 Target ABI/Object policy read-only capability batch

**Base:** exact remote Draft PR #667 head
`fc0cf7e4d5093b72d86f2ee53455ace989414917`.
An independent scan of all 22 production C files and low-frequency model
fields did **not** find a demonstrably dead 20+ line implementation to delete:
the long `.set` clones handle different Section/ABS semantics, while
SourceGraph replay, CFI vs DebugLine and ELF class policies have distinct
consumers. This batch therefore tightens an actually overbroad interface
without inventing an alternative encode/relocation algorithm.

- **One minimal live ABI snapshot:** `MiniAsTargetABIView` carries only a
  `const void *target_state` and the ELF32/ELF64 selector. Common derives
  it at each call; no cached state can become stale after RISC-V `.option`
  changes. It does not contain or return `MiniAs *`, Sections, Symbols,
  SourceGraph or mutable Fragment storage.
- **Seven TargetOps policy types narrowed together:** object attributes,
  semantic Fixup support, Fixup-to-ELF relocation mapping, explicit named
  relocation parsing, final ULEB128 relocation eligibility, CFI ABI and
  symbolic CFI register conversion. Four Targets' 15 concrete callbacks
  now accept the value snapshot instead of a full `const MiniAs *` session.
  Common's MCExpr, MCLayout, CFI, CFI Writer, Object/ELF Writer and `.reloc`
  parser consumers call exactly these callbacks; no parallel policy path
  or compatibility wrapper was introduced.
- **Semantics preserved:** x86 i386 versus AMD64 relocation and DWARF
  register mappings still key on the current object ELF class. RISC-V
  attributes, RV32 CFI stack width and final `NO_RELAX` gate still read
  current Target-private state. `object_attributes` retains its distinct
  `elf_class` argument. The four `encode(MiniAs *, Stmt *)` paths, Target
  directive parsers and CFI mapping-symbol writer remain explicitly outside
  this batch; full P04 is **not** declared complete.
- **Guard:** `probe-mc-target-boundary.sh` checks all seven callback
  signatures, 15 Target implementations and exactly two snapshot fields,
  rejecting direct `as->` session access in those policy bodies.

**Production diff:** 11 `assembler/src` files, `+73/-55` lines (net `+18`),
plus one structural test `+41/-1`. This is an ownership/API contract
change, **not** a claim of net code deletion or ISA support expansion.

**Local proof:** `-Werror` release build; A0 `30/30`; ABI boundary guard;
GNU x86-64/i386 Fixup legality, ELF32 REL, i386 GOTPC shared link,
i386 `.eh_frame`/`.debug_frame`, x86 register-name CFI, x86 deferred LEB
and RISC-V attribute semantic-state oracles PASS. The explicit symbol-zero
relocation oracle passed x86-64 and i386 phases; AArch64 was **NOT TESTED**
locally because the cross assembler is missing. RISC-V cross-GNU
RV32/ULEB, AArch64 GNU machine-byte and LoongArch GNU differential are
likewise **NOT TESTED** locally. Exact-head remote ISA/ABI and four-Target
GNU CI, optional strict 3,536 and Linux Image/QEMU require separate reports;
do not inherit previous-head acceptance.

### 2026-11-01 Common failure-path consolidation (macro/SourceSite/ALIGN/INCBIN)

**Base:** exact remote Draft PR #667 head
`695a8805824e6383648687d9c0e34bbcad61c928`.
This batch converges **23 existing error branches** across four Common
functions; it does not add a parser, relax ISA restrictions, or change how
SourceGraph identities, machine-code bytes or GNU ELF Fixups are produced.

- `source_expansion.c` **`define_macro`**: eleven preexisting allocation,
  invalid parameter and duplicate-definition failures now share one
  `free(copy)` exit. The already-registered partial `MiniAsMacro` remains
  owned by the existing source destructor; diagnostic text is unchanged.
- `source_expansion.c` **`process_source_line`**: both segment interning and
  statement-processing failures share one release/SourceSite restoration
  epilogue. Successful semicolon segments also use that epilogue, preserving
  the original SourceSite for callers and exactly one source text buffer.
- `assembler.c` **`parse_align_args`**: six malformed-expression/width/fill
  failures use one scratch-string release. The original acceptance rules,
  caller error text, fill bytes and maximum-skip treatment are unchanged.
- `assembler.c` **`capture_incbin`**: four seek/size/allocation/read/mutation
  failure paths release the snapshot, file and pathname in one exit;
  `buffer` starts at `NULL` for early failure. A successful snapshot still
  transfers exactly the original bytes once. Its final `fgetc` / `ferror`
  concurrent-mutation check is explicitly retained.

**Production diff:** 2 `assembler/src` files, `+38/-42` lines, net `-4`.
It is a batch of ownership-path deduplication, not a claimed large byte-code
shrink. No public ABI, Target opcode or SourceGraph replay criterion changed.

**Local verification:** `-Werror` release build and A0 `30/30` PASS;
GNU x86 immutable `.incbin` (`49,160` bytes exact), adjacent-DATA alignment,
macro optional/data, SourceGraph cache/replay/stable rebind, SourceContext
composite and x86 Linux page-table content oracles PASS. Both macro failure
cleanup and macro lifetime/diagnostic oracles PASS. The optional sanitized
macro suite reports ASan/UBSan clean but LSan **UNAVAILABLE**, not full
sanitizer PASS. Cross-GNU architectures and remote exact-head CI require
independent verification; strict 3,536 and full Linux Image/QEMU remain
**NOT TESTED** for this batch.

### 2026-11-01 executable ALIGN capability boundary (P04 partial)

**Base:** remote Draft PR #667 head `8b73344e29d4bf7d3e87755680efb768ac4aed0b`.
This is a deliberately bounded P04 interface cleanup, not completion of
the four full Target instruction encoders or a claim of new Linux acceptance.

- **Common owns the mutable session:** `MiniAsTargetOps.append_code_alignment`
  now receives an opaque `MiniAsCodeAlignSink *`, never `MiniAs *`. Only
  `assembler.c` defines or constructs that sink and derives active Fragment,
  output architecture properties and preceding-DATA adjacency from the session.
- **Target permissions are narrow:** the four architecture callbacks can emit
  padding bytes into the active EncodeResult, report first errors, request an
  ELF flag, and query alignment-relevant read-only target/ELF/fragment facts.
  NOP byte tables, RISC-V RVC policy, x86 16/32-bit mode decisions and GNU
  DATA-adjacency policy are unchanged. No Target ALIGN callback dereferences
  `as->`, selects a Section, or receives a mutable `MiniAs` handle.
- **Guard:** `probe-mc-target-boundary.sh` asserts the callback signature and
  opaque definition across all four Targets; the x86 DATA-adjacency GNU oracle
  checks the restricted Common query.

**Production diff:** 6 `assembler/src` files, `+72/-27` lines (net `+45`),
including the Common-owned permission adapters; tests span two existing
scripts. This batch is an API/ownership tightening rather than a net line
deletion. The separate full `encode(MiniAs *, Stmt *)` callback remains in
place: replacing it correctly requires SymbolID/symbol-difference queries,
transactional Fixup and anchor requests, target state and diagnostics across
four encoders, so it must not be represented as already cleaned.

**Local proof:** Release `CFLAGS=-Werror`, A0 `30/30`, the four-Target boundary
guard, GNU x86 adjacent-DATA alignment and paired-alignment byte oracles,
and GNU `.code16/.code32` `38/38` PASS. Local AArch64, LoongArch64 and
RISC-V cross-GNU comparisons are **NOT TESTED** because the needed external
cross assemblers are absent. Remote ISA/ABI, x86/Loong GNU, strict 3,536,
and full Linux Image/QEMU results must be reported for this exact head,
not inherited from the previous commit.

### 2026-11-01 fragment-owned request and dead-write cleanup

**Base:** `ce676c16b405ca225e8ab2cf08f41d0355f3165a` on the sole
`refactor/minias-target-isa-abi-core` branch / Draft PR #667. The commit
containing this entry is this batch's exact source revision. No Linux
acceptance or full 3,536-object strict GNU acceptance is inferred.

This is a deletion/ownership batch, not a new MC algorithm or a copied path:

- **EncodeResult:** fixed-width MCData Fixup APIs no longer accept a caller
  Section selector. Both symbol-name and retained SymbolID paths bind to the
  active Fragment. Staged Fixup requests no longer store a duplicate Section;
  staged synthetic anchors no longer store the Fragment's Section, subsection
  or absolute offset. Common's sole commit reads the authoritative Fragment
  owner. This removes stale shadow state without changing encoded bytes,
  relocation order, or the independent section-owned `.reloc`/DWARF path.
- **MCExpr:** the publicly retained symbol-difference entry point directly
  consumes the one typed AST; its private intermediate pair-copy wrapper is
  gone. Wrapping/absolute/deferred expression evaluation modes are unchanged.
- **MCLayout:** subsection hash allocation no longer zeroes memory twice;
  its fill clears all slots before use. The private layout scan's two callers
  always supply a non-null placement-change result, so dead optional branches
  are removed. Its anchor-graph defensive failure check remains.
- **Source/lifetime:** macro purge no longer zeroes an already-discarded
  array slot that `define_macro` resets on reuse. Inline repeat no longer
  writes NULL into pointers immediately before freeing the pointer array;
  the backing text remains owned by the same `copy` buffer. `minias_destroy`
  no longer zeroes its soon-to-be-dead session after freeing its contents;
  the next source-replay pass performs fresh zero-initialization.

**Production diff:** 6 `assembler/src` files, `+38/-70` lines (net `-32`);
**test diff:** one existing Fragment transaction guard, `+23/-0` lines.
Documentation additions are separate from production code. No public
Invocation API, Target ISA encoding rule, GNU relaxation policy or SourceGraph
replay algorithm was removed.

**Local verification:** Release `CFLAGS=-Werror` build, A0 30/30,
GNU x86 SymbolID/Fixup and `.org` byte oracles, 4,096-section/8,192-subsection
index stress, Source macro lifetime/failure and SourceContext replay,
x86 Linux page-table GNU linked-content oracle, Fragment atomicity guard,
and x86 SourceGraph forced-replay oracle all PASS. Standalone RISC-V GNU
relax oracle is **NOT TESTED** locally (cross `riscv64-linux-gnu-as` absent);
local RV SourceGraph replay PASS with GNU comparison explicitly skipped.
ASan/UBSan macro-lifetime checks reported clean; LSan cannot complete in
this environment. The optional sanitize-mode `-Werror` build hits an existing
`cfi_writer.c` maybe-uninitialized warning; ordinary sanitize build succeeds.
Remote exact-head ISA/ABI and four-Target GNU jobs, optional 3,536 and full
Linux Image/QEMU remain **NOT TESTED** at the time of this entry.

### 2026-11-01 全局回看：删除 x86 ALIGN 的多余 Section 选择参数

`minias_code_alignment_preceded_by_adjacent_data` 只有 x86
Target 调用，而且调用者传入的 `section` 恒为活动 Fragment 的
`stmt->section`。因此删除这个重复参数和 `owner->section==section`
恒等判断，Common 只检查前驱 Fragment 的
`previous->section==owner->section`、子段和真实字节相邻条件。
x86 机器 NOP 策略与原有布局算法都保留。A0 增加独立的 GNU
逐字节对照，覆盖 DATA 后 .p2align、max-skip 以及显式 0x90 填充。
这是对 P04 的一次实际接口收口，不是重构或完成全部 P04。

### 2026-11-01 shared literal decoder failure-path cleanup

The single Common string-literal decoder `assembler/src/string_literal.c` had
eight separate copies of the identical partial-buffer teardown (free, NULL,
reset length, return false). All eight failure paths now converge to one
`invalid:` exit, without changing successful .ascii/.asciz tokenization,
escaping, per-string NUL termination or allocation ownership. The direct
C regression checks successful decoding, empty strings, mixed escapes and
malformed/trailing input; it checks that every failed partial decode releases
and resets the output. The test runs within mandatory MiniAS A0. No Target
ISA code, SourceGraph or ELF semantics changed. Full GNU 3536 strict equality
is neither required nor invoked as the cleanup target.

**Production delta:** 1 file, eight teardown copies folded into one
error exit. A0 exact-head and remote build results must be reported
separately from the new fixture's local compile result.

### 2026-11-01 macro expansion single-owner failure cleanup

`minias_macro_expand` already owned one `done:` release path for
`MiniAsSourceLine`, temporary macro-vararg storage and the copied argument
buffer. Twelve scattered early-return error sites redundantly released
some subset of those buffers. All pre-expansion failures now branch to the
same already-established exit. An unallocated expanded-line vector is
explicitly skipped even when the recorded macro body count is nonzero;
this protects pre-expansion errors from dereferencing NULL. Covered cases
include allocation failure, malformed
keyword/vararg, required argument, percent evaluation and expansion-depth
checks. No change to successful expansion, macro registry lifetime, SourceSite
identity, recursion limit or target parsing.

A mandatory A0 fixture verifies six representative negative paths and the
absence of partially emitted ELF files, complementing existing positive GNU
macro tests and optional sanitizer probes. The full strict GNU semantic
corpus is not part of this cleanup gate.

### 2026-11-01 grouped collector/de-duplication cleanup

The macro parser had *two* identical top-level bracket/quote scans for
comma detection versus whitespace fallback; they now share one
read-only lexical separator helper. The source collector also had separate
macro and repeat nested-body walkers; they now use one matching walk
parameterized only by the two original grammar families. Macro nesting
still matches .macro/.endm; .rept/.rep/.irp still share .endr; the different
1024-byte/256-byte argument limits, label-prefix policy, failure messages,
and normal SourceSite/EOF behavior are unchanged. The old duplicate loops
were removed in the same change.

New mandatory A0 GNU fixture checks actual x86 .text bytes for
macro-with-nested-rept, .rept-with-nested-irp and expression arguments,
plus each of the three unterminated delimiter errors and absence of partial
objects. Existing SourceGraph source-replay, macro lifetime and CFI/ELF
checks remain mandatory. This is source cleanup, not ISA-wide GNU parity.

**Diff:** `assembler/src/source_expansion.c` 1948 -> 1905
lines (net -43); exact-head CI must determine PASS
or failure before claiming completion.

### 2026-11-01 conditional expression parser consolidation

The SourceContext conditional evaluator retained a second lexical
comparison parser that manually walked parentheses/shifts and split
equality, inequality and ordering expressions into two substrings.
The existing Common MCExpr parser already handles all standard operators
with a proper precedence AST. Source .if/.elseif now consumes it directly.
The only retained compatibility exception is historical MiniAS
`.if name=0` (single equals), handled by a narrow top-level fallback;
the standard evaluator remains the sole owner for ==, !=, <, <=, >, >=,
<<, >>, && and ||. Do not expand this exception into all parsers.

A mandatory GNU x86 oracle verifies seven conditional outcomes including
nested logical/shift/arithmetic comparisons and .elseif selection. A
separate MiniAS-only compatibility test guards the historical single-equals
form that GNU GAS itself rejects. Existing SourceGraph replay/conditional
tests remain active.

**Diff:** `source_expansion.c` legacy comparison scan 70
lines -> Common delegator + narrow fallback 46 lines (delta
-24); this is a deletion of a duplicate semantic parser.
No Target ISA, CFI, ELF writer or source-site ownership was redesigned.

### 2026-11-01 grouped MCLayout/x86 parser de-duplication

x86 Target had nine repeated five-field MCInst register-pair initialization
blocks (SSE/GPR transfer, BSF/BSR, IMUL, POPCNT/LZCNT/TZCNT, ALU and
XADD/XCHG). They are now one private helper, retaining the individual
descriptor opcode/width legality. Its absolute and base-index memory
grammars likewise duplicated the same retained NameId and difference binding:
one shared binder replaces both without changing the distinct displacement
fixup policies.

Common MCLayout's scan and post-layout verifier duplicated the same
subsection-local power-of-two alignment/max-skip calculation. One pure
helper is now called by both while stage-specific diagnostics and 32-bit
size checks remain. Neither Target relaxation nor SourceGraph changes.

Mandatory A0 GNU x86 byte oracle checks the affected register families
and .p2align/.balign max-skip. Strict 3,536 GNU corpus and Linux Image/QEMU
were not explicitly triggered. Report exact-head CI, not hypothetical PASS.

### 2026-11-01 x86 register-pair legacy sites removed (second pass)

After the shared x86 register-pair setter landed, a call-site census exposed
13 further copies of the same two-register MCInst shape, previously written
with a different assignment order or without an explicit source width.
Their old blocks were removed: all 13 now use the same Target-private
`x64_register_pair` helper. These paths covered SSE/AVX source+dest
operands, MOVSLQ/MOVZX, system register instructions, and BMI multi-source
operands; the third operands and post-assignment width overrides remain
intact. The helper's false default corresponds to the previous zero-initialized
MCInst operand slot, and any explicit caller override remains after it.

The existing mandatory A0 x86 GNU oracle now adds MOVSLQ, MOVZX and SHA
register-pair bytes alongside prior BSF/BSR/IMUL/ALU tests. Do not weaken
the four-target correctness gate or treat the strictly optional Linux
3,536/corpus jobs as automatically passed.

### 2026-11-01 continued x86 Target register operand ownership cleanup

Full x86 Target call-site search found **47** still-active repetitions
of the same two-field `MINIAS_MC_REGISTER` kind/register initialization
(43 for operand #0, four for operand #2) after the earlier 22
register-pair call sites had already converged. Replace the 47 old
two-field sequences with calls to one private setter; the existing
register-pair helper now delegates to that same setter. The callers
continue to own their register width, target register class, operand
arity, instruction-mode legality and machine opcode. Other Targets
retain their distinct ISA-specific encoders: no Common ISA merge.

GNU x86 exact output regression covers 11 positive unary/control/register
forms, including RDRAND/RDSEED, BSWAP, SETcc, PUSH/POP, MOVABS,
indirect CALL/JMP and TEST, plus the earlier pair/memory tests.
This new regression is added to A0; the current branch SHA must pass
the A0 and x86 GNU CI before any green claim.

**Remaining restrictions (not removed in this batch):** x86 `.code16/.code32`
mode allowlist (requires equivalence proof), Target Encode's mutable
Common session (P04), SourceSite/EOF ownership (P05), P09 `.reloc`
grammar. These are active design or compatibility gates, not confirmed
dead-code restrictions. Do not inflate modified-file coverage into closure.

### 2026-11-01 physical-line restriction removal (SourceGraph)

The physical input collector still used a fixed 65,536-byte `fgets`
buffer, rejected valid long physical GNU source lines, and duplicated
line-length logic around a chunked read. Replace it with one reusable
POSIX `getline` buffer. Keep the same Source-owned physical frame,
private marker validation, C block-comment state, line identity and
one-file-read replay contract. Always release the temporary input
buffer after read or error; distinguish an ordinary EOF from `getline`
failure using `feof/ferror`. No separate Source parser was added.

The new A0 GNU exact-byte fixture contains a single >70 KiB C-comment
line followed by a far conditional branch and 200-repeat source
expansion. It checks the resulting x86 .text and single physical read
under the existing `MINIAS_TRACE_RELAYOUT` mechanism. This removes
one **unnecessary physical input line cap**, not a genuine ISA/ABI
constraint. The per-instruction Target legality, 64 macro argument
slots, x86 code16/32 allowlist and Target encode-ownership restrictions
remain intact until separately proven redundant.

### 2026-11-01 SourceGraph EOF ownership and stale nested-argument limit cleanup

P05 narrowing: the Common source loader used to manufacture a temporary
`MiniAsSourceLine eof` and directly validate `graph->site_count` against
each Fragment's retained SourceSiteID. Both responsibilities now live inside
the existing SourceGraph owner: its EOF site constructor preserves the same
`LINE(parent, line=count+1, segment=0)` identity, and its Fragment-site
validator preserves the existing fail-closed diagnostic. Common continues
to own final literal pools, local .comm fragments and alias/layout updates.
This removes direct SourceGraph site-table inspection from the loader; it
does **not** claim that all remaining P05 Source/Fragment coupling is gone.

The source collector's nested delimiter scan also allocated and discarded
1024/256-byte argument scratch buffers, inadvertently rejecting long
nested directives before they were actually consumed. It now requests
**kind-only** classification (capacity zero), while outer live macro and
repeat argument length rules remain unchanged. This deletes one false
parsing restriction without weakening any Target ISA/ABI feature gate.

A mandatory A0 GNU x86 probe checks a nested long directive in an
unexpanded macro, local .comm EOF-origin materialization, exact .text
bytes and the single physical-read SourceGraph contract. The one redundant
`!emitting_fragment` check in the Target instruction byte guard was
also removed: the preceding check already rejects it while the
fragment-id bounds check remains. P04's live transaction phases and
encode callback are otherwise unchanged.

### 2026-11-01 retire fixed outer collector argument caps

The SourceFrame active collector still used two independent on-stack
`macro_argument[1024]` / `argument[256]` copies for mutually exclusive
directive kinds. One checked per-frame, reusable `minias_grow_array`
scratch buffer now accepts the entire line without an arbitrary copy
limit. Every error branch in this precise source collection function
releases the buffer through one `failed:` exit. The semicolon-inline
collector also stops copying a 256-byte parameter argument that it
never consumes, matching the already cleaned nested kind-only scanner.
The macro/repeat semantic evaluators, source replay, recursion and
Target ISA limits are intentionally not changed.

The mandatory A0 GNU byte oracle checks a 1300-character macro default,
130-element outer IRP and 120-element inline semicolon IRP, with 250
byte-exact values. This removes the old 1024/256 *copy caps*, not a
separately justified argument-count or expression-length constraint.

### 2026-11-01 regression-contract update for retired collector cap

The existing `probe-common-skipped-macro-repeat-gnu.sh` had an explicit
negative test that rejected a 1024-character macro name and a 256-digit
`.rept 000...` argument **only because the old scratch buffers truncated
them**. Both of the identical inputs were independently accepted by
GNU `as --64`. This old test asserted a now-retired implementation cap,
not the GNU language contract. It now compiles both inputs with GNU and
MiniAS and asserts valid object output, preserving all existing skipped
conditional / source replay / cross-Target tests. The new 250-byte
large-collector GNU oracle remains mandatory and separate.

### 2026-11-01 retire per-value IRP SourceFrame reallocation

The `process_irp_block` owner allocated and destroyed an entire
`MiniAsSourceLine[count]` array for every IRP iteration, although each
recursive `minias_source_frame_process` invocation borrows the array
synchronously. The array is now allocated only once per IRP invocation;
only the substituted strings are freed and reset after each value.
A single `done:` exit handles partial substitution and nested-expansion
failures, releasing the strings, line container and argument copy.

Keep GNU comma/space lexing, SourceSite iteration identity, empty
trailing comma and the previous error diagnostics intact. A mandatory
GNU x86 A0 oracle compares 512 IRP values each containing a nested
REPT against 1028 exact output bytes. This is allocation/ownership
cleanup, not closure of remaining P04/P05 phase contracts.

### 2026-11-01 P04 MCData Fixup transaction ownership consolidation

`assembler.c` previously constructed raw `MiniAsEncodeFixupRequest`
records and called the generic internal `minias_encode_result_stage_fixup`
vector append. Its entire MCData staging routine, including the two
existing semantic `minias_add_fixup` / `minias_add_fixup_symbol_id`
entry points, now lives in `mc_encode_result.c` with the Target fixup
staging owner. The raw request-vector append becomes file-local
`static`; its cross-module header declaration is **removed**. Common
still owns only final SymbolID creation, fragment commit and ELF relocations.
The checked Fixup field-width policy moves with the request owner and
remains Common-callable for final Fixup validation; no duplicate width
checker or shadow staging code survives.

This is a concrete P04 boundary reduction, not closure of P04: all four
Target encoders still have `encode(MiniAs*,Stmt*)` entry points, and
the distinct emitting/committing phase guards are still live. Existing
Fixup ordering, NameId/SymbolID timing, diagnostics and ELF behavior
are unchanged by this ownership move. Exact-head A0, ISA/ABI and GNU
Fixup/relocation checks remain mandatory.

### 2026-11-01 fix structural atomic-transaction test for P04 ownership move

The older `probe-mc-fragment-transaction.sh` hardcoded the location
of `minias_stage_common_fixup` in `assembler.c`; that implementation
was deliberately deleted in the P04 cleanup. A0 and GNU byte tests
passed, but this static ownership probe failed on the old source layout.
Update it to check the stronger new invariants instead of skipping it:
the raw EncodeResult append must be `static` and absent from the header,
the old Common MCData request staging must be absent, and the moved
MCData and Target producers must both use one private vector append.
The existing atomic output size/commit guards are preserved. No
production behavior or verification gates were weakened.

### 2026-11-01 P04 active-fragment-only Target fixup and anchor capability

The four Target encoders previously passed a mutable, arbitrary `section`
argument to `minias_encode_add_fixup_ref/abs0`. All **30** call sites
in AArch64, x86-64, LoongArch64 and RISC-V are now owner-bound: the
EncodeResult private instruction validator derives the section solely
from `as->stmts[as->emitting_fragment_id]`. No Target can stage a fixup
into a different section through this capability; offset/width/ISA kind,
NOBITS checks and retained NameId/fixup ordering are preserved.

RISC-V's two private PC-relative anchor calls also stop passing `stmt`
to `minias_encode_request_anchor_ref`. The anchor implementation reads
the sole active fragment itself, retaining the existing fragment kind,
valid section, prefix, ordinal and duplicate-name guards. Both internal
API declarations and all old call sites were updated in the same batch:
there is no parallel compatibility wrapper.

The mandatory atomic MCFragment structural probe now checks all four
Target sources and the internal declarations for these capabilities,
and verifies section/anchor identity is owner-derived. Existing A0
relocation-byte, 4-target ABI, GNU link/ELF and source replay tests remain
required. The `TargetOps.encode(MiniAs*,Stmt*)` callback is **still
broad**: this is a concrete capability reduction, not P04 closure.

### 2026-11-01 P04 target machine-byte write binds to active Fragment

All **24** Target byte-write call sites (AArch64, x86-64,
LoongArch64, RISC-V instruction encoder and RISC-V alignment) now
call `minias_emit_instruction_bytes(as, bytes, size)` without a
Target-selected Section. Common resolves the destination from the
active Fragment before staging, checks that the resolved Section is
valid and not NOBITS, and retains the existing staged byte count,
write ordering, overflow and commit guards. The old Section-accepting
signature and all its callers are removed together, with no shim or
duplicate code emitter. The AArch64 and RISC-V local integer writers
also drop obsolete Section parameters.

The atomic MCFragment structural gate now requires the owner-derived
Section and scans all five Target source files against reintroducing
the wide byte-write API. Combined with the prior section-less Target
Fixup and anchor APIs this closes another concrete P04 mutation
path. `TargetOps.encode(MiniAs*,Stmt*)` is still a broad callback:
this batch does **not** claim complete Encode capability isolation.
Preserve the four-Target A0/GNU/ELF/link/QEMU gates on the exact HEAD.

### 2026-11-01 update target-boundary test after byte output capability reduction

The preexisting `probe-mc-target-boundary.sh` still required the old
`minias_emit_instruction_bytes(as, section, bytes, size)` spelling
in the AArch64 source. The new byte emitter intentionally **cannot
accept a Target-selected section**, so this stale text assertion
failed the exact-head ISA/ABI workflow after the production code had
built with `-Werror`. Replace the old check with a stronger
contract: AArch64 must use the new two-operand byte payload API,
Common must derive Section from the active Fragment, the internal
header must not expose a Section selector, and no one of the five
Target files may pass a Section as a byte-output argument.
Do not skip any existing A0, GNU, semantic Fixup or runtime probe.

## Current refactor dashboard — 2026-11-01 (exact-head recovery and live-path audit)

### 2026-11-01 Source collector single-pass de-duplication (local batch)

`process_source_range` previously copied, stripped comments from and trimmed
each ordinary active source line **twice**: one allocation/pass for macro
opener/closer classification, then another for `.rept/.rep/.irp/.endr`.
The two helper implementations and their duplicated preprocessing were
replaced by one `classify_collector_line` with independently selectable
macro and repeat grammar checks. Ordinary lines do one lexical preparation;
nested macro and nested repeat delimiters still select only their respective
grammar, as does the inline semicolon-repeat collector. Macro classification
retains priority over repeat classification, including its original failure
priority. Macro args remain bounded to 1024 bytes and repeat args to 256;
the latter still recognizes leading `LABEL:` positions and GNU `.rep`.
No conditional frame, forced-site graph or codegen path was removed.

**Production delta:** `source_expansion.c` +104/−135 (net −31); tests
+33/−1; documentation separate. Local Release `-Werror`, A0 **30/30**,
GNU macro lifetime/optional data, disabled `.if` collector, source `.end`,
SourceContext composite, stable replay binding, and x86/RV SourceGraph
replay/branch GNU bytes and symbols **PASS**. The extended skipped-collector
probe checks labelled `.rep` against host GNU, assembles the same Common-only
source under all four Target profiles, and rejects overlong macro and repeat
arguments without producing an object. The optional ASan/LSan mode
was **NOT RUN**, nor were full Linux/Image/QEMU or strict semantic corpus.

The additional 2,206-line ELF Writer/MCLayout/MCEncodeResult and four-Target
TargetOps read-only re-audit found **no proven removable whole pipeline**:
ELF liveness/writing, layout fixed-point/validation and target encoding all
retain different live duties. Do not count that inspection as a deletion or
pretend the remaining P04/P05 ownership seams are solved.

### 2026-11-01 SourceGraph dead state and module-private API cleanup

The SourceGraph/SourceContext call-chain review found no removable second
parser or obsolete conditional replay: persistent forced-branch sites, fresh
expansion frames, inline repeat handling, and `.if/.org/.set` source replay
remain live. Four strictly scoped reductions were made instead:

* `minias_source_graph_line_site` no longer accepts a `segment` selector:
  both production callers always passed zero. The real semicolon statement
  site creation in `process_source_line` still interns `SEGMENT` explicitly.
* The invocation's `source_loaded` flag was equivalent to the persistent
  graph's `file_reads != 0`. Physical loading increments that counter only
  on success; failure exits the assembly without another pass.
* A statement's first semicolon position is reused by the per-segment loop
  instead of rescanning the same unmodified text immediately.
* Macro classification is deferred until after inline-repeat dispatch and
  inactive-conditional handling. The active macro/repeat classifiers retain
  their different label, argument-size and error contracts; no risky
  whole-parser merge was attempted.
* The global symbol scan found five purely same-file services with no
  cross-module consumer: Core destroy, section emission, finalization
  section-alignment request and literal-pool queueing; plus the expression
  operator-token scanner. They are now `static`; the five declarations were
  removed from broad internal headers. The corresponding Target-facing
  ParseContext and PostEmit sink services, and public assembly APIs, remain.
  The direct shared ULEB encoder and the Common SymbolID binding API are
  deliberately still exposed to their real consumers and boundary tests.

**Production delta:** 7 files, +24/−32 lines (net −8); documentation
tracked separately. Release `-Werror`, A0 **30/30**, SourceGraph x86/RV GNU
byte/symbol replay, stable forced-branch rebinding, skipped macro/repeat,
SourceContext composite, `.end` GNU cases, MC ownership/fragment transaction
guards and shared expression grammar **PASS**. No Linux/Image/QEMU or strict
3,536-object semantic gate was rerun; cross-target GNU tests absent from
these scoped probes are **NOT TESTED**, not assumed passing. The changes
do not claim closure of P04/P05 coupling or GNU strict-parity debt.

### 2026-11-01 follow-up — private wrapper/visibility cleanup (`13eac49f`)

The renewed call-site review distinguishes **32 production files scanned for
references** from a completed function-by-function proof. Only three private
surfaces were changed, with their consumers traced before deletion:

* `minias_evaluate_absolute_operand` was a redundant delegating function:
  its two non-null checks are already performed by the one Common
  `minias_evaluate_absolute_expression` owner. Its only consumers (CFI and
  the Target parse service) now call that owner directly; the declaration
  and definition were deleted, without merging different evaluation modes.
* `minias_assemble_with_target` was a non-public legacy forwarding bridge
  whose sole caller selected RISC-V. The caller now fills the identical
  configuration directly. All four `minias_assemble_file*` **public APIs**
  remain intact, as does the canonical `minias_assemble_config` path.
* `minias_find_subsection` has one same-file consumer in `mc_layout.c`;
  its declaration was removed from the broad internal header and the
  function made `static`. The subsection index and lookup are unchanged.

**Local verification on the modified tree:** Release build and A0 30/30
PASS, MCExpr ownership gate PASS, public four-Target invocation PASS, CFI
absolute-expression and 4,096-section/8,192-subsection oracles PASS.
The verified tree was pushed to the original PR #667 branch as `13eac49f`
(8 files total: production +16/−40; tests +10/−1; docs +29/−0).
Cross-Target GNU checks unavailable in some A0 subtests remain skips, not
passes. No Linux Image/QEMU or complete GNU semantic corpus rerun is claimed.
This batch is **private API/obsolete forwarding cleanup, not an architecture
redesign**. The next deep-reading pass should continue seeking genuinely
unused private chains; keep P04 encode, P05 SourceSite/EOF, conditional
replay and historical public entry points until their consumers move.

### 2026-11-01 next cleanup — one checked Common LE32 section patch

The cross-producer byte scan found exactly one remaining duplicated Section
write algorithm in this slice: CFI record-length completion and `.debug_line`
header/record-length completion both encoded a little-endian 32-bit value
directly into existing Section bytes. Both now call a single checked Common
`minias_section_patch_le32` owner, which shares its byte encoder with the
existing LE32 append function. The CFI alignment policy, LoongArch synthetic
length relocations and DWARF-line state machine are **unchanged**. This is
not a second output writer or a new phase boundary.

The shared patch checks Section identity, 32-bit value range, a real data
buffer and `at <= size && size - at >= 4` before writing; it avoids the
old debug-line `at+4` overflow risk. The CFI caller checks at least four
remaining bytes before subtracting the record-length field. A direct
production-code probe covers middle/tail writes, invalid Sections, out-of-
bounds `SIZE_MAX`, value overflow, null data, and no output mutation on
failure.

**Local evidence on the modified tree:** full Release `-Werror` build and
A0 30/30 PASS; GNU exact optimized `.debug_line`/named-view ELF32/64
oracles PASS; CFI source-section collision GNU byte/reloc checks PASS for
x86-64, i386, RV32 and RV64 (three prefixes each). The optional AArch64 and
LoongArch64 cross-GNU CFI checks were **NOT TESTED locally** because their
tools are absent. This batch was subsequently pushed on the same PR #667
branch as `46db7242`. No full Linux Image, QEMU P1, or 3,536 semantic
equality is claimed for that commit. Continue targeted, source-backed
cleanup; strict GNU-all equality is not a product completion requirement.

### 2026-11-01 continuation checkpoint — `4ca932b4`

This checkpoint is **read-only source triage plus local verification**, not a
new production cleanup claim. The branch is still
`refactor/minias-target-isa-abi-core` (Draft PR #667). After the prior
`c1a5f8fa` Linux checkpoint, 26 commits touched 24 production files under
`assembler/src`: **+3,658/−3,712, net −54 lines**. The old Source-owned
directive dispatch and broad Target parse/relax callbacks were removed;
the single SourceGraph/frame collector and MCEncodeResult transaction remain.
The two superseded cached/generated 3×7 workflow YAML files were removed,
**not** the frozen 3×7 validation requirement.

The current head's focused GitHub Actions checks pass, including the ISA/ABI
contract and x86/Loong GNU oracles. Its full Linux 6.6.143, frozen 3×7 and
3536 opt-in jobs were **SKIPPED** and are **not acceptance evidence**. The
last same-commit three-architecture Linux Image/QEMU P0/P1 certificate is
`ae709208`, run `41211136425` (x86-64/AArch64/LoongArch64). RV64 uses a
separate Linux single-toolchain workflow; do not combine different SHAs into
a fictional four-target exact-head certificate.

On this checkout, Release `CFLAGS=-Werror` built successfully and A0 produced
30/30 objects. Some A0 GNU oracles explicitly skipped unavailable cross-GNU
assemblers on the local host; this local result is **not** full cross-ISA GNU
parity. A focused lexical/call-site audit of all 32 current production files
found no obviously orphaned static function or unused TargetOps field:

* P04's broad `encode(MiniAs *)` remains live, but all Targets use the one
  Common MCEncodeResult transaction. Do not create a second one.
* P05's EOF literal-pool/local `.comm` fragment generation and SourceSite
  checks in `assembler.c`, and SourceFrame/SourceSite expansion in
  `source_expansion.c`, are different live responsibilities, **not** duplicate
  implementations. Position-sensitive `.if/.org/.set` replay is likewise a
  currently necessary correctness fallback.
* The public `minias_assemble_file[_target]` legacy entries require an
  explicit external consumer migration before removal; no unverified deletion.

The next **real production batch** must demonstrate an obsolete consumer/owner
that it actually removes, or fix a GNU/real-source counterexample. Accumulate
such changes, then put `[minias-linux-four] [minias-linux-gcc-single]
[minias-replay-3x7] [minias-gate-v1] [minias-semantic-3536]` on one actual
`assembler/**` commit so the expensive Linux and corpus gates verify the
**same SHA**. The 3536 hard gate and the 3536 GNU *semantic* oracle now
have separate opt-in push triggers; they must still be reported as two
different checks. Preserve run IDs, object denominators,
GNU baselines, P0/P1 and skipped/error states in the next checkpoint.

### 2026-11-01 production batch — shared DWARF section serialization

The follow-up targeted audit found **three genuine duplicated encoding
loops** in two live producers, not dead P04/P05 callbacks: CFI and
`.debug_line` separately serialized ULEB128, SLEB128 and little-endian
32-bit section values. The loops are removed and both producers call the
existing Common section owner through one LE32/ULEB/SLEB byte contract.
Source `.uleb128` retains negative-unsigned rejection; a new full-width
`uint64_t` encoder preserves DWARF values above `INT64_MAX` without casting
them to signed. No new section writer, CFI event model or SourceGraph exists.

Production scope: **4 files, +56/−57 lines (net −1)**. The direct encoder
boundary regression is added to mandatory A0 (new `.c` + `.sh` fixture),
including `UINT64_MAX`, `2^63`, `INT64_MIN`, signed 127 and negative-as-unsigned.
Local `-Werror` build, 30-object A0, x86/i386 GNU CFI section byte/reloc
oracles, ELF32/ELF64 `.debug_line` GNU named-view/size and CFI linker probes
pass. The four other GNU CFI cross profiles and full Linux/corpus gates must
still be judged from **same-head CI**, never inferred from local skips.
See architecture decisions §26. This is a serialization de-duplication
batch, **not** closure of P04, P05, P09 or P14.

### 2026-11-01 exact-head CI checkpoint — `cb0b4917` (not full semantic parity)

The push carries all five opt-in markers on one production commit. On this
**same SHA** the frozen 3×7 replay (run `41266803599`, 21 shards / **9,645**
inputs) **SUCCEEDED**, but means `ASSEMBLED_UNVERIFIED`, not GNU semantic
equality; the 3,536-object hard gate (run `41266803618`) **SUCCEEDED**;
four-target GNU GCC single-AS runtime ladder (run `41266803620`), original
BusyBox four-target runtime (run `41266803595`), and GNU A/B runtime (run
`41266803600`) **SUCCEEDED**. The Linux 6.6.143 multiarch full runtime run
`41266803638` was **IN PROGRESS** at this checkpoint; do not reclassify until
its actual result is recorded. Optional/skipped workflows are not evidence.

The newly reachable **strict 3,536 GNU semantic** workflow run `41266803582`
**FAILED**. It is independent of the green hard gate:

* C first500 MiniC generation: **496/500 produced, four compiler failures**
  (indices 0, 22, 47, 50; indirect-call declaration argument mismatch),
  **before the assembler comparator**. The semantic C shard jobs omitted
  `-fshort-wchar -funsigned-char`, flags supplied by the existing original
  Linux hard-gate first500 replay. The C149 semantic generation step also
  omitted those Linux Kbuild options. Restore both flags in the next workflow
  batch, then establish the actual strict GNU oracle denominator; do **not**
  label a failed MiniC generation as a MiniAS instruction mismatch.
* Native35: **29 DIFF, six MINIAS_FAIL, zero GNU_FAIL**. All six fail on
  the intentionally unsupported `.option arch,+zbb`; this is an explicit
  ISA policy gap, not permission to silently accept unknown features. The
  29 GNU comparisons were configured with `-mno-arch-attr` while MiniAS
  emits `.riscv.attributes`, guaranteeing a section/attribute difference;
  however, real `.text` length and relocation differences also remain.
  Align the oracle attribute policy explicitly and inspect the remaining
  per-object diffs; never declare all 29 false positives.

Next order: **(1)** restore exact Kbuild MiniC options in semantic replay;
**(2)** run first500/C149 again with explicit generation and comparison
denominators; **(3)** separate ELF attribute-policy comparison from real
instruction/relocation diffs; **(4)** implement a scoped, GNU-tested
`.option arch,+zbb` target-site feature policy, retaining strict A0 negative
tests until replaced by feature-specific legality tests. Continue recording
one exact-head success/failure/skip matrix and artifact IDs per batch.

### Next grouped cleanup — locally verified; new exact-head CI still pending

The original DebugLine implementation sorted each finalized `.loc` anchor
twice in one emit: once to resolve GNU named views, then immediately again
to emit `.debug_line`. Both copies validated identical anchors and used the
same section/offset/subsection/ordinal ordering. The emitter now builds and
sorts **once**, resolves strict views on that immutable snapshot, and emits
from it; MCLayout's separate public view resolver remains intact. Error
ownership and the strict failure checks remain unchanged. The RISC-V Target
also now projects the immutable branch/JAL `bound_symbol` from one helper,
while relaxation and final encoding keep independent current-symbol lookups
and diagnostics. No phase or object owner was merged or deleted.

The semantic oracle's GNU reference now **retains** `.riscv.attributes`
(removes only `-mno-arch-attr` from `-Wa,-mno-relax,-mno-arch-attr`), matching
MiniAS' emitted attributes rather than ignoring this ABI-visible section.
The semantic C shard and C149 replay commands also pass both missing MiniC
Kbuild flags. No semantic dimension or `--require-equal` condition was
weakened; remaining native35 instruction/relocation differences and six
`.option arch,+zbb` failures remain live open work.

Local **after these changes**: Release `-Werror` **PASS**, A0 **30/30 PASS**;
GNU x86 ELF32/ELF64 named views and optimized real-GCC DWARF5 loclists
byte-exact, view-size/reject GNU oracle, stable Source rebinding, x86 `.org`
lexical layout and RISC-V tiered branch expansion **PASS**. The RISC-V GNU
branch comparison still **SKIPPED (cross-GNU unavailable)**. Parser/YAML
static checks **PASS**. Full 3,536 strict GNU semantic re-evaluation and
Linux multiarch/QEMU outcomes for the new revision remain **PENDING**.

### 2026-11-01 same-head `7bc9b65c` returns — hard PASS, semantic RED

At this checkpoint, exact-head frozen 3×7 (run `41267765236`, all **21**
shards and aggregate, 9,645 candidate `.s`) and hard gate (run
`41267765007`, **3,536/3,536**) **SUCCEEDED**. This is still only the
assembled/hard-gate contract. The fixed Linux MiniC options produced all
**3,352 C shard plus 149 C149 inputs**. Strict GNU oracle (run
`41267765088`) then reported **215 PASS / 3,309 DIFF / 12 MINIAS_FAIL /
0 GNU_FAIL / 0 INCONCLUSIVE / 0 ORACLE_ERROR** across 3,536 inputs:

* C3,352: 63 PASS, 3,283 DIFF, 6 MINIAS_FAIL (dominant symbols differences
  require a common-cause diagnosis rather than 3,283 ad hoc patches).
* C149: 147 PASS, two DIFF under
  `arch/riscv/kernel/vdso/{hwprobe,vgettimeofday}.i.s` (symbols+relocs).
* Native35: 5 PASS, 24 DIFF, six MINIAS_FAIL on `.option arch,+zbb`;
  enabling GNU attributes eliminated only five formerly uniform DIFF.

This run's independent RV64 `linux-single` job **SUCCEEDED** for Linux
Image assembly and Python-era runtime/P1 assertions. The separate
`41267765128` x86-64/AArch64/LoongArch64 full GNU Kbuild→MiniAS→GNU
link→QEMU workflow was still **IN PROGRESS**. Do not infer its result or
fold older SHAs into this head's Linux certificate.

Later same-head job-level evidence in `41267765128`: **x86-64 SUCCESS**
(2,841/2,841 production objects, 3,109 MiniAS invocations, MiniAS and GNU
both QEMU P0 2/2 + P1 12/12); **LoongArch64 SUCCESS** (2,748/2,748
objects, 3,004 MiniAS invocations, QEMU P0 2/2 + P1 12/12); LoongArch
and AArch64 independent GNU baselines SUCCESS. The **AArch64 MiniAS**
job subsequently also **SUCCEEDED**: 4,171/4,171 Kbuild assembler
objects, 4,415/4,415 MiniAS invocations, no bypasses, QEMU P0 2/2 and
P1 12/12. Together with the same-SHA `41267765088` RV64 linux-single
success, all four real Linux targets have P0/P1 success on `7bc9b65c`.
The four targets use two separately triggered workflows; no result may
be transferred to later SHAs.

### Next in-progress production deletion — duplicate alias DAG refresh

`parse_loaded_source_graph()` called
`minias_finalize_subsection_layout(as)` and then redundantly ran
`minias_refresh_symbol_aliases(as)` again. Finalization itself refreshes
the alias DAG after assigning subsection bases; its subsequent `.size`
end-anchor adjustment updates no symbol value/section/type or source size
used by alias resolution. Remove the outer second walk; preserve
finalization's error path and its one necessary alias refresh. This saves
one O(symbols+edges) graph traversal and two temporary allocations on
each normal source graph parse/replay.

Local **after deletion**: Release `-Werror` PASS, A0 **30/30 PASS**;
GNU x86 exact alias
DAG test **2,048 edges PASS**; 1,024 SymbolID relocations GNU-exact,
absolute fixup GNU-exact, Source rebind and `.org` lexical GNU-exact PASS.
Exact-head Linux tests for this *next* revision must be re-run after the
rest of the grouped source cleanup is ready; the previous
`7bc9b65c` certificate does not cover this unpushed change.

The same pending batch also replaces `minias_validate_fragment_layout`'s
quadratic `for Section × for Subsection` end-position check with one ordered
Subsection cursor. The canonical anchor graph already validates the sorting
and SectionID; gap/overlap, overflow, and empty Section checks remain.
The 4,096-Section / 8,192-Subsection GNU oracle, reversed Subsection `.org`
snapshot and layout ownership tests **PASS** locally. GNU RISC-V deferred
fragment relaxation and branch binding also **PASS** after the GNU cross
toolchain was provisioned locally; the earlier unavailable-tool skip is no
longer the current state. This is an executed validation-pass optimization,
not a new layout model.

The strict semantic gate's dominant C failure is **not** a single wrong
symbol spelling: for example `fs/proc/util.i.s` has equal `.text` size
(2,032 bytes) and identical `.riscv.attributes`, but GNU emitted only five
relocations while MiniAS emitted 45, mostly unnecessary local PC-relative
HI20/LO12 fixup pairs and their private anchors. The C149 independent
`vdso/hwprobe.i.s` likewise has GNU 14 versus MiniAS 80 relocations.
The local cross-GNU oracle now proves a narrower, safe fix: GNU GAS with
`-mno-relax` folds **disposable `.L*` temp labels**, not arbitrary ELF
`STB_LOCAL` symbols; named locals and `.set` aliases keep HI20/LO12 pairs.
The pending RISC-V encoding patch folds only same-section, defined, strong,
non-aliased `.L*` references whose PC-relative AUIPC+ADDI immediates fit the
exact signed HI20/LO12 range. Cross-section, weak, undefined, named local,
alias and overflow stay on the relocation path. Independent GNU probes also
exposed and fixed the existing PIC `la` policy: **all** PIC `la` use
GOT_HI20 even for defined locals/addends; RV32 loads a GOT slot via LW,
RV64 via LD; PIC `lla` remains PCREL. Default GNU linker-relaxation emits
additional markers, which MiniAS deliberately does not claim to implement.

New mandatory GNU probe `probe-rv-local-la-gnu.sh` reports **PASS** for
no-relax `.text` bytes, local fixup removal, retained named/alias/weak/extern
relocations, +/-2048 rounding, RV32/RV64 PIC GOT/addends and overflow
behavior. The integrated Release `-Werror` and A0 **30/30 PASS** with GNU
cross tools. This is a **small, proven subset**, not a claim that all 3,309
historical DIFF have disappeared. Do **not** disable symbol/relocation
comparison or fire the 3,536 full gate until this grouped change is ready.

### 2026-11-01 `0af59539` semantic return; next scoped Zbb batch

The actual re-run on `0af59539` confirms the local-LA fix affects
real Linux C assembly, not only synthetic tests: C149 improves from
**147/149 to 148/149** GNU semantic equality (`hwprobe.i.s` fixed);
`vgettimeofday.i.s` remains a `.riscv.attributes` metadata-only difference
despite byte-identical `.text` and equal relocations. Example C shards
improve `next500` **31→185/500**, `new500` **11→125/500**,
`first500` **10→97/500**, `next500b` **3→18/500**. Full strict 3,536
comparison (run `41270318021`, all nine GNU comparison jobs completed)
totals **696 semantic equal / 2,828 semantic DIFF / 12 MINIAS_FAIL /
0 GNU_FAIL / 0 inconclusive / 0 oracle error**. Relative to `7bc9b65c`
this is **+481 exact semantic matches**, but remains a RED strict gate.
The failing native35 cases still include six unsupported scoped `+zbb`
inputs; first500 has six other MiniAS failures. Aggregate remains RED,
without weakening `--require-equal` or any ELF semantic dimension.

On the same `0af59539` SHA, **3,536/3,536 hard gate** (run
`41270318037`, 11/11 jobs) and **frozen 3×7** (run `41270318023`,
22/22 jobs including 21/21 shards) **SUCCEEDED**. Those confirm assembly
and frozen replay only, not GNU equality. Its independent RV64
`linux-single` job (run `41270318021`) subsequently **SUCCEEDED**:
2,099/2,099 Kbuild object matches, 2,225 MiniAS passes, expected 14
compatibility probes and one compiler query, no bypasses, QEMU P0 init/sh
and P1 12/12 PASS. The x86/AArch64/LoongArch64 Kbuild/QEMU run
`41270318051` was still **IN PROGRESS** at this checkpoint; its
results require separate terminal evidence.

Later job-level results for that **same** `0af59539`: x86-64 MiniAS
**SUCCESS** (2,841/2,841 Kbuild objects, 3,109 passes, QEMU P0 2/2
and P1 12/12); LoongArch64 MiniAS **SUCCESS** (2,748/2,748 objects,
3,004 passes, QEMU P0 2/2 and P1 12/12), with no GNU assembler
fallback. LoongArch independent GNU baseline **SUCCESS**. AArch64 MiniAS
and its GNU baseline were **IN PROGRESS** at this later snapshot.
The latter subsequently both **SUCCEEDED**, including AArch64 MiniAS
4,171/4,171 Kbuild objects, 4,415 passes and QEMU P0 2/2 + P1 12/12.
Matched production Kconfig provenance also succeeded: run `41270318051`
is a complete four-target P0/P1 certificate on `0af59539` (with its
separate same-SHA RV64 single run), not a certificate for later SHAs.

The next targeted production change permits **only** scoped
`.option arch,+zbb` through the existing RISC-V Target option stack and
`stmt->site.mode_bits`, without modifying base ISA/ELF attributes or
Common. It is GNU byte-tested under RV32/RV64, including pure Base-I
profiles, nested push/pop and post-pop failures. Unknown options and
strict Base-I executable-word audit remain fail-closed. New GNU oracle is
mandatory A0, local Release `-Werror` and A0 **30/30 PASS**. It does
**not** implement full Zbb; only the already-supported `ctz`, `orc.b`,
`rev8` forms can benefit. Native35's six files must be re-measured on
the next head, not assumed fixed before real input tests.

Independent native35 evidence inspection confirms the prior six
`lib/purgatory` `strcmp`/`strlen`/`strncmp` failures each stopped first at
`unsupported-isa-option:arch,+zbb`. They represent only **three distinct
input SHA256s** reused by purgatory. Historical CI artifacts preserve
manifest/semantic reports but **not** their six original `.s` byte streams;
the current local six-real-input oracle is therefore **SKIP, not PASS**.
The next same-head native35 CI must regenerate the inputs and evaluate
both acceptance and strict GNU semantics.

On the pushed `564f7c33` exact-head GNU semantic run `41271305459`,
the regenerated native35 **does** clear all six historical parse
failures: **5 semantic PASS / 30 DIFF / 0 MINIAS_FAIL**, versus the
previous 5/24/6. The six `lib`/`purgatory` cases remain semantic DIFF,
not GNU byte-exact certificates. This supersedes the local six-input
SKIP for *acceptance*, but not for strict semantic parity.

The pending batch also fixes the remaining C149 `vgettimeofday.i.s`
**metadata-only** divergence: a GNU-verified source CSR/privileged mnemonic
requires RISC-V Tag_File priv_spec 1.11.0. One Target-owned attributes
builder now emits the explicit or inferred Tag 8/10/12 fields, using the
existing immutable pre-layout Fragment view for source-form inference;
there is no second ELF byte-appending implementation or Common policy.
GNU RV32/RV64 `-mno-relax` oracle **30 exact objects + 10 expected
rejections PASS**, and the real regenerated `vgettimeofday.i` has both
`.text` and `.riscv.attributes` GNU byte-exact. Release `-Werror` and
A0 30/30 PASS locally; only remote next-head semantic replay can certify
the expected C149 **149/149** result. See architecture decisions §33.

The pushed `564f7c33` **has now certified C149 149/149** on remote
strict GNU semantic oracle `41271305459`: 149 semantic equal, **149
strict equal**, zero DIFF/assembler failures/inconclusive/oracle errors.
This is a genuine closed C149 subset, not 3,536-source completion.
Across all 3,536 inputs on that same SHA the nine semantic jobs report
**723 equal / 2,807 DIFF / 6 MINIAS_FAIL / 0 GNU_FAIL** (604 strict
equal): **+27 equal**, **−6 MiniAS fails** relative to `0af59539`.
The first500 still has six other MiniAS failures; 2,807 semantic
differences are not waived. Its aggregate necessarily remains RED.
Its independent frozen Linux 3×7 run `41271305472` also **SUCCEEDED**:
all 21 shards and 22/22 jobs. This is assembly-only coverage, and the
new SHA's **3,536/3,536 hard gate** run `41271305536` subsequently
**SUCCEEDED** (11/11 jobs). These are distinct from GNU semantic
equality. Its RV64 linux-single job (run `41271305459`) subsequently
**SUCCEEDED**: 2,099/2,099 Kbuild objects, 2,225 MiniAS passes,
14 expected compatibility probes, no bypasses, QEMU P0 init/sh and
P1 12/12. The other architecture jobs on `564f7c33` were still
running at this checkpoint.

### Next grouped production correctness batch — RV64 `li` size convergence

The `564f7c33` remaining C3352 GNU DIFF include a reproducible,
high-frequency **machine-byte** mismatch: the previously shared
`rv_materialize_li` retained plan was correct as a single layout/encode
owner, but emitted inefficient non-GNU sequences. Three frozen Linux
source examples explain their complete `.text` length differences:
`fdt_early.i` **+84 B** (redundant ADDIW after LUI and successive wide
LI growth), `sifive/errata.i` **+48 B** (two long negative constants),
and `kernel/cpu.i` **+16 B** (unsigned `4294967295`). Those original
objects had equal symbol/relocation counts; size drift propagated into
section, relocation and symbol offsets.

The pending Target-only fix reuses exactly the existing retained
`MiniAsRiscvLiPlan` and GNU signed12/shift algorithm. The common
measurement and Target final encoder still consume the **same** word
plan; bounded arrays and uint64-defined arithmetic protect worst-case
64-bit values. The three regenerated source `.text` sections now match
GNU byte-for-byte at **3,656/3,656**, **7,460/7,460**, and
**23,088/23,088** bytes; the first two full ELF semantic comparisons
strict PASS, while `kernel/cpu` has only an independent strict
`section_alignment` dimension. A new mandatory GNU RV32I/RV32IMA ×
RV64I/RV64IMA `li` oracle tests signed and unsigned boundaries, 64
deterministic wide constants, `.size`/CFI/relocations and RV32 overflow.
Independent QA additionally tested **1,024/1,024 random 64-bit
constants** byte-exact. Release `-Werror` and A0 **30/30 PASS** on
the integrated local tree. The existing A0 `li` golden was updated
*only after* a GNU `-mno-relax` byte oracle proved the new sequence.

This next batch does **not** certify RV64GC compressed multiword `li`:
GNU RVC may compress its internal LUI/SLLI despite `-mno-relax`
(observed GNU 34 B vs MiniAS 42 B); `.option norvc` is byte-exact.
Documented as a separate RVC Target sizing item, not hidden by
comparator filtering. See architecture decisions §34. Exact-head
3,536 semantic and Linux P0/P1 remain **PENDING** for this unpushed tree.

The above LI batch was pushed as `27a620da`, tree exactly verified
against the local commit. On this SHA, **3,536/3,536 hard gate**
(run `41271812214`, 11/11 jobs) and **frozen 3×7**
(run `41271812121`, all 21 shards and 22/22 jobs) have **SUCCEEDED**.
The 3,536 GNU strict semantic run `41271812069` and real Linux 6.6.143
QEMU runs were still **IN PROGRESS** at this checkpoint. No LI-related
semantic improvement is booked until their exact-head comparison returns.

That remote `27a620da` strict semantic corpus has now completed all
nine comparison jobs: **1,766 semantic equal / 1,764 DIFF / 6
MINIAS_FAIL / 0 GNU_FAIL / 0 inconclusive / 0 oracle error**; 1,286
are strict equal. Compared with `564f7c33`, the single shared LI policy
fix gained **1,043 more semantic-equal objects**; examples include
first500 **111→388/500**, next500 **194→460/500**, new500
**127→348/500**. The six surviving MiniAS failures are the known
`.option arch,+v` first500 subset, not LI plan failures. This is a
substantial but **still RED** strict semantic result; no dimension was
removed from the GNU comparison. RV64 Linux and multiarch QEMU remain
separate exact-head gates.

The same `27a620da` run `41271812069` also finished its independent
RV64 **linux-single SUCCESS**: 2,099/2,099 production Kbuild objects,
2,225 MiniAS passes, 14 expected compatibility probes, zero bypasses,
QEMU P0 boot and P1 **12/12** PASS. This certifies the LI batch's
RV64 real Linux execution only; other target jobs are not inferred.

### Next targeted zero-hard-failure batch — scoped V sites

All **six** `27a620da` first500 `MINIAS_FAIL` are the exact same
`unsupported-isa-option:arch, +v` diagnostic, across five RISC-V
kernel/KVM vector uses and `kernel/sched/core`. Each of the six frozen
preprocessed `.i` inputs reproduces through MiniC with original Kbuild
`-fshort-wchar -funsigned-char` flags, and GNU accepts all six assembled
`.s`. The emitted code uses `vsetvl` (5), `vsetvli` (9), `vle8.v`
(16), `vse8.v` (16) and `vmv.v.i` (4), which already have typed Target
encoders. No need for a broad fallback or silent option acceptance.

The pending RISC-V-only patch grants `SITE_V` through the existing
instruction-site option stack, preserving ISA/file attributes and Common
ownership. GNU RV32I/RV64I exact probes cover scope, nested Zbb+V
push/pop, bytes, attr stability and fail-closed unknown options. GNU
also proves scoped `+v` implies F/D/Zicsr feature permission in
addition to V, but **not** M/A/Zifencei; only the immutable effective
site mask receives that verified closure. Strict Base-I still rejects
the emitted V words. In six real local source oracles, MiniAS now
assembles **6/6**, yet five remain `mapping_symbols_compatible` DIFF
and `kernel/sched/core` has additional genuine section/reloc/symbol
differences. Full 3,536 GNU equality is **not** claimed by removing
the former parse failures. See architecture decisions §35.

The exact-head local GNU A0 oracle for this pending patch reports
**8/8 byte-exact RV32I/RV64I cases and 38/38 negative cases**, with
GNU `-march=rv*i2p1` pinning to avoid I2p0's legacy implicit CSR
permissions. All eight file ISA attributes remain unchanged. Both
Release `-Werror` and A0 **30/30** pass; full remote Linux and 3,536
semantic results are still required on the pushed SHA.

Follow-up source owner audit of all 32 production files found **no
additional high-value deletion with proven safe phase semantics**.
NameId vs SymbolID indexes, SourceGraph vs MCLayout replay and x86
measure vs encode have distinct consumers; the small RISC-V register
number parser repetition has marginal benefit, while repeated target
relax-plan queries would require a new cache/lifetime model. Those
paths are **audited/retained**, not artificially removed to inflate
cleanup progress. The next priority remains GNU-real-source correctness.

This top section is the **current progress snapshot**. Later sections retain
their original 2026-10-29 audit/batch evidence as history; their “pending CI”
labels refer to those historical snapshots, not the current branch HEAD.
Progress numbers have three independent meanings: **read**, **modified**,
and **architecturally closed**. A file changed once is not necessarily done.

| Measured scope | Current count | Meaning |
| --- | ---: | --- |
| MiniAS `assembler/src` production code | **32** files | 22 C + 10 headers; excludes test/docs/CLI/shared ELF |
| Modified since audited `1d9db17` (2026-10-29) | **29/32 = 90.6%** | 26 existing modified + 3 new SourceGraph files; **code-touch coverage only** |
| Unmodified production files | **3/32** | `invocation.c`, `mc_lexer.c`, `target_registry.c`; do not change stable code to inflate coverage |
| Recent convergence from `ccf939f` | **24/32 = 75.0%** | 24 commits including parse, GNU binding, scoped relaxation, SourceContext and CFI; independent recent-iteration footprint, **not** total completion |
| Full historical audited baseline | **35/35 read** | At `1d9db17`: 29 `assembler/src`, CLI, 5 shared ELF files; older source-read certificate, not a claim that every current line was reread today |
| Shared CLI/ELF audited neighbors | **2/6 modified** | `elf/include/minielf.h`, `elf/src/relocatable_writer.c`; unchanged consumers retain independent roles |
| MiniAS-facing tests | **212 tracked files** | Includes the shared DWARF LEB unit/oracle files; not part of the production denominator |

The 99 commits after `1d9db17` changed 29 current source files with
**+11,855/−6,325** lines in the baseline-to-HEAD diff (net +5,530).
That includes file splits/new SourceGraph functionality; it is **not**
“5,772 lines of dead code eliminated”, nor a measure of correctness.

### Open architecture/correctness decisions (stable P01–P16 ledger)

| State | Count | IDs and next condition |
| --- | ---: | --- |
| **Closed** | **6** | P01 ISA legality versus strict-I audit; P06 AArch64 typed parse-site mapping provenance; P11 RV GLOBAL/WEAK branch/jump GNU relocation and link parity; P13 Common Fixup/fragment staging; P15 explicit Target object finalization; **P16 GNU source/CFI collision on six profiles** |
| **Partially closed** | **4** | P04 parse/measure/post-emit/pre-layout/site/relax phase contracts, **encode remains broad**; P05 source-owned conditional/macro routing, fragment SourceSite/EOF boundary remains; **P09 GNU symbol-zero `.reloc` now correct, full grammar not attempted**; P14 RV E ABI owned, AArch64 dot/name debt remains |
| **Untriaged concrete correctness gaps** | **0** | The previously unproved P16 source collision is now GNU-confirmed and closed; new gaps are recorded only from real programs or explicit GNU byte/reloc oracles |
| **Deferred on purpose** | **5** | P02/P07 scoped RV `.option arch`; P03 versioned extension profiles; P08 COFF/Mach-O/endian model; P12 object-format-neutral binding type |
| **Audited, retained** | **1** | P10 Target local pre-resolution and ELF retained-fixup division is intentional |

This is a *fixed issue register*, not an estimate that MiniAS is only
“5/16 finished”: the existing assembler/ELF/ISA engines and large prior
refactors precede these 16 gap IDs. The remaining work has unequal sizes.

| Data-flow boundary | Current result | Remaining work |
| --- | --- | --- |
| SourceGraph/source-site replay | One cached physical reader/frame collector, stable identity, Source-owned conditional/macro routing | Remaining fragment SourceSite/EOF provenance interface (P05) |
| MCExpr/typed DATA, SectionKey/Subsection | Shared owners and lookup paths in place | New syntax only when real sources prove a gap |
| Fragment encode/Fixup commit | Single Common staging and creation ownership | Preserve explicit `.reloc` and DWARF/CFI-specific operations |
| Target ISA/ABI | Four Target families, RV32/RV64 XLEN+ABI, strict-I separated | Scoped `.option arch` and further ISA families when needed |
| Target phase interfaces | Opaque Common parse services for four Targets; read-only post-emit, pre-layout, measure and RV/x86 relax services; site snapshot/alignment read only Target state | Encode callback still sees mutable Common root; reviewed single Common staging transaction retained |
| Object output | One ELF32/ELF64 writer through shared `minielf` | COFF/Mach-O and endian are **future**, not current blockers |
| Real-program validation | Previous exact-head `18547c4` mandatory CI **8/8 SUCCESS**; current focused GNU CFI/.reloc/A0 oracles local PASS | Full Linux kernel and 3536 opt-in suites not certified by this batch; this new exact-head CI separately pending |

The four-Target measurement seam has now been replaced as one unit:
`TargetOps.measure_mcinst` takes an immutable Target-state view and an
opaque Common diagnostic sink. AArch64 and LoongArch consume retained
MCInst/Target lowering plans; RISC-V's RVC candidate and x86's legacy
prefix planner are **shared by measure and encode** without a duplicate
machine-width algorithm. See architecture decisions §18 and the structural
guard `probe-mc-target-boundary.sh`. P04 remains **partial** because encode
still needs full Common-owned object operations.

The audited Common parse capabilities are now one opaque, Common-owned
`MiniAsParseContext` threaded through the four actual Target parsers and
their private helpers (§20). It delegates to the **existing** symbol,
expression and literal-pool algorithms, retaining one grammar, one
allocation owner and one parser path. The old wide `parse_mcinst(MiniAs*)`
callback is deleted, including its dynamic RV form parser signature.
The focused GNU and A0 tests are green on the integrated tree.

The encode/relax audit found **no duplicate Common encoding transaction**:
the single `MCEncodeResult` path already owns Target/Common byte and Fixup
staging. The concrete RV branch-symbol binding and AArch64 direct LDR
correctness defects were fixed instead. GNU source-section bytes, exact
relocation fields and the linked strong-over-weak symbol override all agree
for six RV profiles (RV32/RV64; 20 relocations each) and the AArch64 LDR
fixture (four PC-relative loads plus one literal-pool relocation). Existing
x86/LoongArch target paths were audited and retained. See architecture
decisions §21 and the new mandatory CI oracles.

P04 relaxation now uses one opaque, Common-owned `MiniAsRelaxContext` for
both RV and x86 (architecture decisions §22). Only symbol lookup, checked
difference and first-error diagnostics cross that phase boundary; the
old `MiniAs*` relaxation callback and broad branch-helper signatures are
gone. Each Target still has **one** distance/encoding policy shared by
relax and final encode. This batch changes 8 production files with
**+114/−36 lines (net +78)** and strengthens the existing ownership guard.
Local Release `-Werror`, A0 **30/30**, RV GNU binding **6/6**, x86 GNU
branch/binding and `.code16` **38/38**, both SourceGraph replays and
AArch64/LoongArch GNU probes all PASS.

P05's high-value statement-routing portion is now migrated as one batch:
Common no longer parses the Source-owned `.if*`/`.elseif`/`.else`/`.endif`,
`.purgem`, macro invocation, `.altmacro` or `.end` commands. Its old
dispatch blocks and the corresponding broad Source header declarations
are deleted; one FILE/EXPANSION collector and one SourceGraph remain.
Physical block-comment activity is now loader-local, and the single macro
allocator/destructor closes two error-path leaks. A checked dynamic
vararg buffer also fixes a reachable `.altmacro` expansion overflow.
See architecture decisions §23 and dedicated composite GNU replay and
macro-lifetime probes. This batch changes **4 production files,
+216/−195 lines (net +21)**; the statement lowerer alone is **+14/−137**.

The next GNU-source correctness batch now closes **P16** and narrows P09:
the CFI writer aligns both generated CIE streams at the physical section
boundary (not the record start), shares the old duplicated initial-CFA
policy, emits GNU RV/LoongArch debug FDE-range relocation pairs and
LoongArch's linker-relaxable debug CIE/FDE length fields via typed synthetic
section fixups. The strict GNU oracle covers six Target/ABI profiles ×
three source-created section shapes, **18/18 exact section bytes and
relocation sites/types**, plus real LoongArch GNU-LD debug-frame byte
parity. P09's GNU two-operand `.reloc` now uses ELF symbol index zero
through the single existing Fixup store; x86-64/i386/AArch64 and the
previously correct `.reloc .` / `.reloc .+2` GNU oracles pass. Two scripts,
five fixtures and existing CI-step extensions accompany **5 production
files, +127/−56 lines (net +71)**. See architecture decisions §24.

Next coherent batch: choose **P04's remaining encode capability boundary**
or **P05's remaining SourceSite/EOF cross-owner dependency** only where a
real caller/owner deletion follows. More `.reloc` grammar remains P09
partial until a real failing GNU program demands it. Do not add a second
SourceGraph or rewrite a working Target for a superficial percentage gain.

---

## Historical full-source audit — 2026-10-29

Status at that baseline: **full source read completed; first integrated
architecture/correctness batch verified locally; exact-head CI pending**.
Pinned pre-change tree: `1d9db179f59a66505c7910a1668daee5e0defb0e`,
single branch `refactor/minias-target-isa-abi-core`, Draft PR #667.

This is an explicit coverage and issue ledger, not a declaration that MiniAS
has passed GNU object parity, complete Linux linkage, or QEMU P1. All six
source domains were independently read in full; the integration owner read
the CFI/ELF/driver domain and checked cross-domain contracts. A search hit,
file inventory, or a green unit test alone never counts as source review.

## 1. Exact source coverage

The 35 files below total **30,122 lines** at the pinned pre-change tree.
`Complete` means the entire file was read, including error handling, fallback
paths and teardown, rather than just definitions found by textual search.

| Owner/domain | Production files (pre-change line counts) | Lines | Read |
| --- | --- | ---: | --- |
| Common source | `assembler.c` 6459; `expression.c` 428; `string_literal.c` 167; `invocation.c` 51 | 7,105 | Complete 4/4 |
| MC model | `minias.h` 46; `minias_internal.h` 880; `mc_expr.c` 1846; `mc_layout.c` 1116; `mc_lexer.c` 63; `mc_operand_semantics.c` 58; `mc_encode_result.c` 136; `expression_grammar.h` 59 | 4,204 | Complete 8/8 |
| RISC-V | `riscv_encode.c` 1992; `riscv_target.c` 700; `riscv_profile.c` 167; `riscv_profile.h` 36; `riscv_target.h` 111; `riscv_fixup_kinds.h` 16 | 3,022 | Complete 6/6 |
| x86-64 | `x86_64_target.c` 3572; `x86_64_fixup_kinds.h` 14 | 3,586 | Complete 2/2 |
| AArch64 | `aarch64_target.c` 5169; `aarch64_fixup_kinds.h` 34 | 5,203 | Complete 2/2 |
| LoongArch64 | `loongarch64_target.c` 2020; `loongarch64_fixup_kinds.h` 15 | 2,035 | Complete 2/2 |
| Object/debug/CLI | `cfi.c` 392; `cfi_writer.c` 450; `debug_line.c` 473; `elf_writer.c` 559; `target_registry.c` 38; `tools/minic-as/main.c` 56; `elf/include/minielf.h` 227; `elf/src/binary_export.c` 155; `reader.c` 508; `relocatable_writer.c` 960; `rewrite.c` 1149 | 4,967 | Complete 11/11 |

This inventory includes the full MiniAS production C/header implementation,
its CLI and direct shared ELF library consumers. Tests, workflows, historical
design documents and unrelated MiniC/MiniLD source are **dependency review**,
not part of the 30,122-line production-source denominator. The test and CI
inventory is separately checked against each implemented fix.

## 2. Established architecture constraints

- Common owns source events, symbol identity, MCExpr, persistent Fragments,
  fixed-point layout, generic emission, CFI/debug, ObjectModel and ELF writer.
- A Target owns ISA/ABI features, operand dialects, typed forms, instruction
  relaxation, machine bytes, private FixupKind and relocation policy.
- RV32 and RV64 deliberately share one RISC-V Target with distinct XLEN/ABI;
  x86, AArch64 and LoongArch remain separate ISA backends.
- The fifth unrelated Target must not require a Common ISA switch, private
  register shape or a copied section/ELF writer.
- No parallel legacy/new implementation is kept without an explicit migration
  and deletion plan. Every patch receives a before/after dependency audit.

## 3. Full-domain findings and evidence

| Priority | Domain / actual code | Finding | Closure requirement |
| --- | --- | --- | --- |
| P0 | Source replay: `assembler.c` ~4470–4506, ~6323–6380; `mc_layout.c` ~362–372, ~777–817 | Forced long branches cross full reparse by statement ordinal; SourceGraph records identity drift but does not fail. Changed conditional topology can reassign a forced branch to a different statement. | Stable source-site plus MC-shape keys, explicit missing/ambiguous fail-closed handling; remove ordinal list, preserve conditional and symbol ordering. |
| P1 | `assembler.c` ~6185–6290; `mc_expr.c` ~1658–1780; `mc_encode_result.c` | Instructions/exec alignment use an EncodeResult, while DATA/FILL/SPACE and some other fragments append directly to Section. Compatibility append state remains in `MiniAs`. | Unified fragment-level emission transaction without erasing distinct `.reloc`/CFI section-owned fixups. |
| P1 | `assembler.c` ~948–976, ~2655, ~3122 | `.fill` element-by-element and `.space`/zero padding through small repeated appends impose avoidable work on large sources. | Overflow-safe reserve and bulk fill preserving NOBITS and target alignment. |
| P1 | `assembler.c` ~260–320 | Section identity lookup remains a linear scan; thousands of named/COMDAT sections can be quadratic. | Index the full SectionKey, not the name alone; preserve first-definition semantics and distinct SectionIDs. |
| P1 | `mc_expr.c` ~1415–1416, ~1517–1518 | Symbol, addend and section/dot offsets use unchecked signed arithmetic in some emit paths. | Use one checked numeric API and test positive/negative extremes. |
| P0 | `x86_64_target.c` ~2980–3275 | Defined same-section GLOBAL/WEAK CALL/JMP/Jcc/RIP targets are sometimes folded without GNU-required relocation; symbolic MOV memory immediate uses inconsistent ABS32 vs ABS32S across duplicate paths. | Unified target-private branch/binding and memory-fixup plan, GNU bytes + offset/type/symbol/addend matrix. |
| P1 | `riscv_target.c` ~14–47, ~426–476; `riscv_encode.c` ~776–857, ~1065–1110, ~1210–1252 | RVC alignment is not coupled to immutable site feature state; `.option relax` lacks R_RISCV_RELAX/ALIGN contract; LI/LA use `MCInst.encoding` for semantic metadata; CSR zimm can be mistaken for RV32E GPR. | Single typed Target form/context, explicit supported option policy and GNU profile/ELF tests. |
| P1 | `aarch64_target.c` ~2160–2268, ~2291–3926, ~4915–5120 | Branch expression grammar duplicates Common arithmetic; a long instruction-family parse chain and differing `.arch` vs `-march` capability entry points remain. | One Target-owned profile parser, typed Common expression references and Target descriptor families. |
| P0/P1 | `loongarch64_target.c` ~1457–1493, ~1726–1864 | Branches to WEAK symbols can be fixed up as local, B16/B21 expansion is incomplete, legitimate 32-bit `li.w` constants are rejected; some parser `encoding` writes are recomputed by typed lowering. | GNU-backed weak-binding/long-branch/LI.W oracles, one Target lowering plan, deletion of stale encoding state. |
| P1/P2 | `minias_internal.h` ~221–274, ~469–574, ~780–826 | Generic `register_64` and broad `MiniAs*` Target callbacks retain ISA and lifetime coupling even though memory payloads and register banks are now private. | Introduce narrow read-only Target views and an emission sink after the fragment transaction closes. |
| P2 | CFI/debug (`cfi.c`, `cfi_writer.c`, `debug_line.c`) | Separate serializers correctly own different DWARF streams but repeat primitive little-endian/ULEB/SLEB appends; some grow helpers duplicate capacity policy. | Share only byte-buffer primitives and checked capacity logic; do not merge CFI and line-state semantics. |
| P2 | Shared ELF (`elf/src/{reader,relocatable_writer,rewrite}.c`) | Multiple bounded ELF read/write/rewrite roles are appropriate; byte order, alignment and small buffer algorithms are repeated. The assembler writes a completed ELF image directly to the output path. | Audit utility reuse and atomic output failure behavior with Reader/Objcopy/Strip/AR/NM regression gates; do not combine distinct consumers merely to reduce files. |

## 4. Baseline proof and limits

- On pristine `1d9db179`, local `make MODE=release check-minias-a0`
  completed successfully, `MINIAS_A0=PASS objects=30`.
- GNU cross-assembler parity cases reported `SKIP(toolchain-unavailable)`
  locally where the corresponding host cross toolchain was absent; do not
  reinterpret these lines as GNU PASS.
- Exact-HEAD PR #667 remote MiniAS ISA/ABI workflow `40878237981` and
  x86/Loong GNU workflow `40878238132` completed successfully; their target
  steps were inspected separately.
- Earlier 9,645/9,645 original Linux inputs were **assembled, unverified**
  at a different head. Full GNU object semantic parity, complete Linux link,
  Image and QEMU P1 are not certified by this architecture audit.

## 5. Implementation and sign-off protocol

1. Keep the pinned reading inventory immutable. For every changed source,
   record the diff and re-review all its callers and dependent contracts.
2. Fix a whole subsystem/family at once, including all currently exposed
   source failures; remove replaced paths in the same batch. Record **added
   and deleted production-code lines**, separately from tests/docs/CI.
3. Before exact-head CI, run Release/A0, target-appropriate GNU positive and
   negative oracles, cross-layer ownership guards, CFI/ELF consumers and
   sanitizers where available. A missing cross GNU tool is NOT TESTED.
4. Preserve one MiniAS branch and Draft PR #667. Do not merge or declare the
   3×7/full ELF/Image/P1 strict gate passed until the same exact source tree
   has produced independent evidence.

## 6. First integrated batch — independently verified local evidence

The findings table is the *pre-change issue inventory*, not a list of still
unfixed defects. This batch deliberately combines independently audited
domains into one production-source change and retires replaced paths in the
same patch. Production additions and deletions are counted separately from
tests, documentation and workflow edits.

| Finding | Local disposition | Remaining boundary |
| --- | --- | --- |
| P0 Source ordinal/replay binding | Replaced with stable expanded SourceSite plus typed MC shape; old cross-replay ordinal API deleted. Actual numeric-local and macro `\\@` generation drift now accepted, missing/ambiguous source site fails closed. | A disappearing forced site still rejects instead of incrementally updating dependency topology; 1,024-pass guard remains. |
| P1 Common bulk data append | `.fill` and `.space` use checked one-reservation fill/pattern operations; zero-repeat UB removed, NOBITS semantics preserved. | Direct section append vs fragment-owned emission transaction is still distinct. |
| P1 MCExpr overflow | Defined modular wrap for unsigned 64-bit data/RELA and checked signed S+A-P; illegal cross-section division rejected. | Broad range proof of all indirect expression consumers remains open. |
| P0 x86 symbol binding | GLOBAL/WEAK branch and RIP relocations preserved, ABS32/ABS32S selection corrected; duplicate memory emit and Fixup routes unified. | Instruction descriptor registration still has redundant parallel tables. |
| P1 RISC-V Target state | LI/LA pseudo shape moved off raw encoding cache, CSR opcode family table unified with typed zimm, RV32 Zbb `rev8` corrected; RVC alignment and ELF flag use ISA capability independently of `.option norvc`. | GNU `R_RISCV_RELAX`/`ALIGN` semantics are not implemented and must not be advertised as done. |
| P1 AArch64 profile and deferred delta | `-march`/`.arch` share Target parser; signed `end-begin-4` forward difference and branch offset now keep the subtraction direction. | Large instruction parser and private expression dialect still need convergence. |
| P0/P1 LoongArch LI.W and binding | Typed `LI.W` lowering replaces redundant encoding cache; valid negative immediates accepted, WEAK same-section B16 retains relocation. | Far B16/B21 branch expansion requires a real GNU oracle; no speculative code was kept. |
| P1/P2 section index, fragment emission, TargetOps, CFI/ELF primitives | Audited and recorded, **not closed**. | Next subsystem-scale batches, each gated by full regression. |

Integrated local gates on the shared modified tree:

- Release `-Werror`, `check-minias-a0`: **PASS, objects=30**.
- GNU host x86 `probe-x86-symbol-binding-fixups-gnu.sh`: exact `.text` and
  offset/type/symbol/addend for **14 relocations**; malformed REX/SIB rejected.
- `probe-mcexpr-signed-overflow.sh`: GNU bytes, RELA and wrap semantics exact.
- `probe-mc-source-stable-rebind.sh`: two replays, branch/source drift,
  numeric-local renumbering, macro counter renumbering, disappearing-site
  fail-closed, `.fill`/NOBITS GNU byte agreement.
- RISC-V RV32 width, RV32E CSR zimm and RVC/ELF flag local probes: **PASS**;
  GNU cross-assembler comparison **NOT TESTED locally**.
- Six x86 GNU CFI/frame regressions and GNU DWARF-line comparison **PASS**;
  ELF link-order/section index with **1,024 sections** GNU-equal.
- AArch64/LoongArch GNU oracle and ELF reader/objcopy/strip consumer scripts
  require unavailable cross assemblers in this local environment: **NOT
  TESTED**, not a pass. Corresponding workflows must run on the exact pushed
  commit before these claims can be closed.

The batch is intentionally not a declaration of complete GNU semantics,
original Linux full object parity, final Image link or QEMU boot success.

## 7. Next integration: parse-time SectionKey / Subsection indexes (pending CI)

The prior 35-file reading inventory remains the audited source baseline. This
follow-up is an actual cross-file change to the Common parser and MCLayout, not
a claim that all Target architectural debt is complete.

- **SectionKey identity:** one open-addressed, allocation-checked index holds
  the complete (name, nullable COMDAT signature, nullable LINK_ORDER source
  anchor) key. The original SectionID, first-declaration type/flags, alignment
  updates and appending to a group-specific Section are unchanged. No
  name-only lookup is used to resolve the full key.
- **Subsection identity:** a second hash uses (immutable SectionID, GNU
  subsection number). The table is refreshed immediately after MCLayout sorts
  the owned subsection array, avoiding stale ordinal aliases while retaining
  the old sorted binary-search path for layout/fixups.
- **Failure ownership:** a failed allocation does not publish a Section or
  Subsection slot. Both indexes have explicit context destruction; the ELF
  writer's independent SectionName index is retained because its ordering and
  lookup contract is different.
- **Oracle:** the GNU section/subsection test covers 4096 SectionIDs, 8192
  push/pop reopens, COMDAT same-name variants, LINK_ORDER cross-products,
  nullable link-order identity and first-declaration merge entsize.
- **Four additional probes** independently exercise x86 ModRM/register forms,
  user-owned versus generated .debug_line, and LoongArch alias/privileged
  families, all wired as fail-closed GNU steps in the existing fast oracle job.

**Verification at authoring time:** code blobs and workflow changes prepared;
no exact-head Release/A0/GNU/21-shard outcome has yet been obtained for this
batch. Do not promote the newly wired probes to PASS until a run on its actual
commit SHA has succeeded. Existing unresolved P1 items remain Fragment
emission transactions, conditional dependency graph, and target-wide form
deduplication. GNU runtime/Image/QEMU are separate correctness tracks.

## 8. Exact-head Section/Target/Debug correction and next source-audit findings

**Validated source HEAD `02c4eae95c61a80dec0d3f189d94e3bc70db2d9a`**

- MiniAS Target ISA/ABI Contract: run **40892956318**, SUCCESS.
  Both the RV32/RV64 native strict/A0 real-link job and the AArch64
  real-LD/QEMU job reported SUCCESS on that exact HEAD.
- MiniAS x86/Loong GNU Oracle: run **40892956449**, SUCCESS, job
  **123514355880**. The four newly wired GNU probes are success:
  **63** x86 typed ModRM forms / **6** negative cases with exact GNU
  instruction bytes and relocations; **4096** unique Sections,
  **8192** indexed Subsections and **8192** hot push/pop switches,
  including distinct COMDAT/LINK_ORDER keys, GNU ELF section payload
  exact; user-owned/nonempty and synthesized `.debug_line` must not
  silently concatenate, but empty explicit sections work; LoongArch
  **5** psABI alias instructions, **9** privileged forms,
  **2** PCALA relocations, **11** GNU invalid cases and **2**
  intentionally MiniAS-only unsupported relocation combinations,
  with exact GNU bytes/relocs for the supported forms.
- Combined delta relative to `005e9b63` (before this next initialization
  change): six commits, **six** modified production C/header files,
  **+318/-115 production lines**; tests, CI and docs are separate.
  Those patches removed the duplicate x86 register opcode table, mapped
  LoongArch GPR `$s9` and FPR ABI bank aliases, added complete
  Section/Subsection indexes, and rejected dual-owner DWARF line tables.
- The Loong GNU probe itself found a false negative assertion: GNU GAS
  syntactically accepts some misplaced PCALA modifiers. Preserve MiniAS's
  fail-closed supported-domain policy, but NEVER report that syntax as
  a GNU rejection. The revised test names the two populations separately.

**Additional whole-module lifecycle finding:** initial Section creation
in `minias_init_for_target` ignored allocation errors and attempted
subsection creation or Target initialization with incomplete Common
state. Make initialization return a checked result, preserve context
teardown and prohibit parse/output after a failed init. This extra
change is NOT covered by the above exact-head results until its own
new revision is run.

**Open structural debt, explicitly not closed by this batch:**

1. Common Fragment byte ownership is still split: DATA/FILL/SPACE and
   nonexecutable padding append directly, while instructions/code ALIGN
   stage and commit EncodeResults. A unified ownership migration must
   preserve SymbolID/Fixup ordering, CFI and .reloc section fixups,
   NOBITS semantics, and fail-closed partial objects across all Targets.
2. SourceGraph keeps immutable physical SourceFrame and stable expansion
   site IDs but lacks dynamic conditional read sets / semantic
   dependency graph; full state is reparsed in exception cases.
3. RISC-V link relaxation R_RISCV_RELAX/ALIGN is a specific ABI policy,
   not a generic Common instruction option. It is NOT fully supported.
4. AArch64 has many source-mnemonic `strcmp` families (439 call sites
   across the target, but not all are duplicates); a data-driven Target
   form catalog with independent GNU negative/error-path oracles is the
   right next complete-Target refactor, rather than blindly rewriting
   individual comparisons.
5. ELF/CFI and object writer must retain distinct serialization and
   output-ownership roles even if shared checked grow/ULEB primitives
   are consolidated.
6. Original Linux 9,645 GNU input AS coverage is not the same as
   complete original GNU object parity, final Image link or QEMU P1;
   those are separate correctness milestones.

## 9. Source architecture comparison added (documentation-only)

See `architecture-comparison-llvm-gas-2026-10-29.md` for the
current exact-head 35-file, 30,735-line LLVM MC / GNU GAS comparison,
ownership map, concrete newly exposed issues, and the next large
subsystem refactor order. This is not new production-code acceptance.
High-confidence new cases reproduced on the host GNU x86 assembler:
ELF section `unique,<number>` identity, and `.section` `?`
inheritance of COMDAT. RISC-V `.option relax` accepted with no
per-site semantic change and no R_RISCV_RELAX/ALIGN implementation
is a confirmed source-level contract gap; GNU cross-oracle is pending.

The 35-file baseline and prior six-domain read are historical coverage
claims. This comparison reruns whole-inventory/module-boundary scans
and focused call-chain review; it does not claim separately scheduled
subagents or an independent fresh line-by-line pass of each file.
