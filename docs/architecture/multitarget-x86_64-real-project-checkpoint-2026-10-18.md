# Multi-target x86-64: first two real-project checkpoints (2026-10-18)

## Scope / baseline

- Baseline: `main@2102a0894110ff3af7d6cf5e67811dc561f063d2`.
- Feature PR: [#524](https://github.com/yituanxing/minic-toolchain/pull/524), kept as draft pending the final source/ABI review; RV64 focused and full Linux regression have now been evaluated.
- The legacy default remains `riscv64-linux`. Target selection in the native `minic-cc` now explicitly carries one target model from parsing to Core and codegen.
- The outer staged `minic` driver still owns only the RV64 tool pipeline and must fail closed if asked for x86-64.
- x86-64 generates GNU-compatible x86-64 ELF assembly; GNU preprocesses, assembles, links and supplies CRT/libc. No claims about MiniAS/MiniLD x86 support or GNU-free x86 bootstrap.

## Verified real-project acceptance

[Existing scoped regression job, run #39105644338](https://github.com/yituanxing/minic-toolchain/actions/runs/39105644338) executed on an x86-64 Linux runner. The feature-branch-specific job step **succeeded**, verifying:

1. Native `minic-cc` build with `-Werror`; target-selection and RV64 default/explicit assembly parity smoke.
2. `check-x86_64-m1`: original simple returns/calls/branches plus static record and function pointer relocations, Core `RECORD_COPY`, indirect SysV calls, binary64 arithmetic and NaN comparisons, integer/double conversions, binary32 transport, and SSE argument/result ABI.
3. `check-x86_64-tinyaes`: frozen vendor AES-128 ECB, MiniC vs GCC native run, both exit 0 and stdout/stderr byte-exact equal.
4. `check-x86_64-cjson`: unchanged pinned cJSON 1.7.19 compiled in full by MiniC; existing parse-object-array-print-roundtrip runtime harness returned 0; existing upstream `test.c` GCC/MiniC differential returned 0, stdout/stderr byte-for-byte equal (stdout 873 bytes).

Commands to rerun the bounded native acceptance on x86-64 Linux:

```sh
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/multitarget \
    build/multitarget/bin/minic-cc
make MODE=release CFLAGS=-Werror BUILD_DIR=build/multitarget \
    check-target-selection-m0 check-x86_64-m1 \
    check-x86_64-tinyaes check-x86_64-cjson
```

The cJSON harness reuses the existing `tests/external/cjson/probe.sh` and
`final_differential.sh` with a thin x86 invocation adapter; the pinned
upstream files, preprocessing assumptions, GNU oracle, and test vectors were
not changed.

## Current-source RV64 regression certification (2026-10-18)

**Focused toolchain qualification:** [run #39106379457](https://github.com/yituanxing/minic-toolchain/actions/runs/39106379457) **SUCCESS**. The existing focused job rebuilt the Mini toolchain with `-Werror` and passed the current RV64 compiler/driver checks, MiniAR A0/A1, ELF reader, MiniNM, MiniAS, MiniLD static/dynamic/script, and linker section GC. The pre-existing Linux workflow YAML validator was temporarily repaired *on this feature ref* and passed both Ruby YAML and actionlint in that run.

**Fresh full Linux regression:** [run #39106577232](https://github.com/yituanxing/minic-toolchain/actions/runs/39106577232) **SUCCESS**. This is a real new **current-source RV64 MiniC** compile of Linux 6.6.143, not a replay of old candidate objects:

- Seven independent Kbuild C-object producer shards: **295 + 295 + 295 + 295 + 295 + 295 + 294 = 2064/2064**. All seven succeeded; `MINIC_NATIVE_PROFILE=PASS`, `COHORT_COMPILE=PASS`, `COHORT_ABI=PASS`, `COHORT_PRODUCE_ONLY=PASS`.
- The manifest pin for `src/compiler/compiler.c` was refreshed from the old source blob to the actual multi-target candidate blob **`47b666aab4c53db7d00bd43d327bfa70cfa08ffa`**. Other existing pinned source identities were not changed. This is an identity check, not evidence on its own.
- Independent receiver authenticated seven objects groups: `GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`; `COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`; `COHORT_ABI=PASS objects=2064`.
- Original GNU final link, reused verified same-run newly generated 2064 objects: `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`.
- **QEMU P1 passed**, `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`; final `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`.

The run opted into the historically certified RV64 `MINIC_LINUX_FULL_CROSSBLOCK=1` and `MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1` profile. It demonstrates **current checked-in sources compiled by the host toolchain targeting RV64, linked with GNU, booted in QEMU**, *not* a new nine-tool Stage4 self-hosting qualification. Stage4 fixed-point and a full native nine-tool union were not re-executed.

**CI hygiene:** The original committed `linux-runtime-gcc-baseline.yml` contains unrelated malformed dangling YAML/duplicate job text, which blocked ordinary dispatch and linting. For the above runs, the pre-existing workflow was temporarily repaired **only on the feature branch** and the original workflow content was subsequently restored byte-for-byte; the normal workflow on main remains an independent maintenance issue. No CI workflow modifications are carried in this feature diff. Separate x86 native acceptance (#39105644338) and fresh RV64 full P1 (#39106577232) are both documented with exact run IDs.

## What the real projects made necessary

- tiny-AES: integer arithmetic, byte tables, pointer addressing, control
  flow, signed/unsigned remainder and division.
- cJSON: static aggregate records holding function pointer relocations,
  `FUNCTION_ADDRESS`, `RECORD_COPY`, first-class indirect calls,
  binary32/binary64 payloads and arithmetic, signed/unsigned numeric
  conversions, independent x86-64 integer and SSE argument register banks,
  floating returns, and variadic vector register count in `%al`.

The x86 codegen remains intentionally conservative, with eight-byte Core
value slots, explicit object storage, and no register allocator or machine
IR. It must not silently take unsupported ABI classes (by-value aggregate
calls, general by-value aggregate argument classification, long double, fully general inline asm, nested
global aggregates, vector instructions, or generalized x86 toolchain
routing) through a convenient but incorrect register path.

## Follow-on architecture decisions

- Keep shared C semantic lowering and Core IR stable; fix actual target
  leaks rather than creating a generic framework before a second user.
- Keep target data layout, register classes, calling convention and
  instruction selection owned by the targets. The shared Core
  `RECORD_COPY` semantic operation is implemented by each emitter.
- Audit compiler-generated and source-level optimization transformations
  separately before defining any `-O0/-O1/-O2` system.
- Next candidate workloads can validate deeper ABI/ELF cases or expose
  genuinely duplicated machine-level algorithms before extracting a shared
  Machine IR. Do not refactor the frozen RV64 codegen in the absence of
  evidence and RV64 regression coverage.

## Post-regression x86-64 correctness fixes and real-project frontier

**Differential bug reproduction:** [#39109177487](https://github.com/yituanxing/minic-toolchain/actions/runs/39109177487) compiled the candidate x86-64 emitter and ran the new native over-alignment test against GCC. MiniC returned **1**, GCC returned **42**. The original x86 frame builder aligned the growing allocation cursor *before* adding an object's size, but objects are addressed at negative `%rbp` offsets. Even 16-byte alignment was therefore violated; alignments above 16 also cannot be guaranteed by the SysV `%rbp` invariant.

**Correctness repair:** For normal at-most-16-byte alignment, the object *ending offset* is aligned relative to `%rbp`. For stricter alignment, the x86 backend reserves extra space and aligns `%rsp` dynamically, locating objects from that aligned base while retaining fixed `%rbp` Core slots. The ABI/ELF test now compares native MiniC and GCC for 16/32/64-byte locals. ELF object visibility tests also compare `readelf -W --symbols` against GCC and verify `HIDDEN`, `PROTECTED`, and `DEFAULT` global object properties.

**Verified:** [focused #39109352021](https://github.com/yituanxing/minic-toolchain/actions/runs/39109352021) and [focused #39110494351](https://github.com/yituanxing/minic-toolchain/actions/runs/39110494351) succeed with `-Werror`, native alignment/ELF oracle, RV64/AR/LD focused suite, and unchanged tiny-AES/cJSON byte-exact differential gates.

**zlib 1.3.1 pressure:** The pinned *original upstream* tag points to commit `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`; the new `tests/external/zlib/probe-x86_64.sh` validates Git blob identities and builds/runs GCC's original `test/example.c` first. The x86 emitter gained a limited flat array-of-record static initializer with per-field ABI padding and function-pointer relocations (as required by `deflate.c`'s `configuration_table[10]`). It also gained SysV stack-passed scalar arguments, including separate 6-GPR / 8-XMM register overflow classification and 16-byte-aligned outgoing stack areas, for both direct and indirect calls and incoming callee parameters. Tests run 8-int, 10-double, mixed-class and indirect stack-argument calls.

The **historical first zlib frontier was 5/15 translation units** through `gzclose.c` under [focused #39110494351](https://github.com/yituanxing/minic-toolchain/actions/runs/39110494351): `adler32.c`, `compress.c`, `crc32.c`, `deflate.c`, `gzclose.c` assembled on x86-64, while `gzlib.c` fails in the *shared frontend* at preprocessed line **741:14**: `use of undeclared local`, during the `lseek(state->fd, 0, 2)` expression. This diagnostic was later identified exactly using `CORE_FAST_TRACE`: the unresolved name was `lseek`, not `state`; the raw unconfigured zlib `zconf.h` lacked the upstream configure-time `unistd.h` detection. No parser bug was present for this particular failure.

**Lua 5.5.0 pressure:** Existing pinned official release and native GCC build check passed. MiniC first fails at `lapi.c` / `lua_pushfstring` on Core instruction **46, `VARIADIC_ARGUMENT_ADDRESS`**. This is an x86-64 SysV variadic-prologue / `va_list` ABI feature, not an excuse to emulate RV64's variadic model. Real x86 `va_list` classification and register-save-area semantics need a separate target-driven design before development.

These results support the existing decision **not** to extract a shared Machine IR, spill manager, or generic ABI table prematurely. The RV64 Linux 2064/QEMU P1 certification cited above belongs to the earlier feature candidate; after the new *x86-only* emitter and test changes, the **focused RV64 regression was rerun**, but full 2064/QEMU P1 was not repeated. This distinction must be maintained until a future explicit merge gate.

## New regression and zlib 14/15 checkpoint (2026-10-18)

**Exact reproduction before changing shared code:** [#39111596025](https://github.com/yituanxing/minic-toolchain/actions/runs/39111596025) emitted `FRONTEND_LOOKUP_DETAIL name=lseek`, proving the earlier `gzlib.c` undeclared-name failure was an **unconfigured upstream zlib feature-detection artifact**. The harness now checks the pristine upstream Git hashes, executes the unmodified upstream `./configure` to generate `zconf.h` (including host `unistd.h` detection and supported large-file feature flags), and invokes both GCC and MiniC in the same C11 language mode. No preprocessor-only fabricated function declarations and no vendor-source edits are used. An experimental GNU11 mode independently showed a separate missing `__builtin_bswap16` builtin from host system headers: GNU11 coverage is **not** claimed by the current C11 gate.

**Actual shared-parser defect:** The configured `gzread.c` encountered `unsigned const char *q`, accepted by GCC but rejected by MiniC because the integer-specifier loop previously stopped at interleaved `const`. The shared `src/frontend/parser_type.c` now consumes interleaved `const`/`volatile` with integer keywords (e.g. `unsigned const char`, `long volatile int`), with RV64 and native x86 parser/runtime regression fixtures. This is a correct C grammar fix, not an x86-specific workaround. [#39112180312](https://github.com/yituanxing/minic-toolchain/actions/runs/39112180312) passed the new x86 runtime fixture (42) and progressed to the genuine variadic ABI blocker.

**Full translation-unit survey:** The zlib harness now offers opt-in `ZLIB_X86_64_SCAN_ALL=1`, recording the complete set of unsupported units without changing any upstream source. The first survey [#39112332996](https://github.com/yituanxing/minic-toolchain/actions/runs/39112332996) measured **12/15** native object compilation, with three distinct gaps:
- `gzwrite.c`: `gzprintf` requires Core kind **46**, `VARIADIC_ARGUMENT_ADDRESS`.
- `trees.c`: `static_ltree` flat array of records with selected union scalar initializers; original object had 576 initializer slots and 576 union selections.
- `zutil.c`: `z_errmsg` array of 10 pointers to strings requires an ELF relocation per array element.

The x86 code emitter was extended **only** for these observed global-data cases: validated element-level relocations in scalar/pointer arrays and selected scalar union members within a record array. In both cases the target DataLayout determines field sizes, padding, and relocation width; unhandled nested aggregates still fail closed. Native regression fixtures `pointer_array_reloc` and `union_record_array` both returned 42 on Linux x86-64.

**Historical 14/15 snapshot:** [focused #39112825888](https://github.com/yituanxing/minic-toolchain/actions/runs/39112825888) **SUCCESS** and `ZLIB_X86_64_FRONTIER compiled=14 total=15 blocked=1`. All zlib translation units except `gzwrite.c` were compiled by MiniC, assembled by GNU and validated individually; GCC independently built and ran the unmodified upstream `test/example.c` reference. **Full zlib executable runtime differential has NOT run** because the 15th object is missing. This is not a claim of complete zlib acceptance.

### Confirmed next ABI boundary: System V variadic callee

Two **independent real projects** now hit the *same* missing capability: zlib `gzprintf` and Lua `lua_pushfstring` use C varargs, first exposing `VARIADIC_ARGUMENT_ADDRESS`. This is a strong signal to implement a **target-owned x86 System V va_list model**, rather than pulling the mature RV64 prologue into a premature generic backend.

The present shared frontend maps `__builtin_va_list` to a pointer; Core lowering treats `__builtin_va_arg` as an 8-byte cursor dereference/advance, which is sufficient for the existing RV64 model but **not equivalent** to x86-64 System V's `__va_list_tag` (general-purpose register offset, SSE register offset, overflow argument pointer and register-save-area pointer). Simply emitting an x86 address for Core kind 46 would silently miscompile both projects.

Requirements before implementing this feature:
1. Represent target-specific builtin `va_list` semantic type and its ABI layout in the target model without altering RV64 behavior.
2. Define Core semantic operations for `va_start`, `va_arg(type)`, `va_copy` and `va_end` that do not bake in an 8-byte pointer-cursor assumption.
3. Implement the x86 SysV variadic callee register-save area, separate GPR and XMM offset tracking, stack overflow transitions, and accurate `%al` vector argument count at call sites.
4. Start with native differential tests for integer, pointer, double, mixed classes, register exhaustion, stack overflow and `va_copy`; then run *unchanged* pinned zlib upstream example and Lua tests.
5. Only after repeated real-project evidence, consider extracting truly target-independent Core analyses. **Do not add a Machine IR or a shared ABI allocator just to accommodate this feature.**

The current updated PR includes modifications in shared `parser_type.c`. The historical Linux 2064/QEMU P1 receipt is from before this latest shared-frontend change; **a new full RV64 Linux 2064/QEMU P1 run remains a merge gate**. Focused RV64 tests alone cannot certify this shared change.

## Full zlib 1.3.1 runtime gate and typed SysV varargs (2026-10-18)

**Source-driven ABI implementation:** zlib `gzprintf` and Lua `lua_pushfstring` independently exposed Core `VARIADIC_ARGUMENT_ADDRESS`. Under x86-64 Linux System V, the backend now allocates the genuine 176-byte GP/SSE register-save area and 24-byte `__va_list_tag` cursor per `va_start` instruction, with correctly computed `gp_offset`, `fp_offset`, `overflow_arg_area`, and `reg_save_area`. This makes forwarding a native `va_list` to glibc `vsnprintf` ABI-correct. All existing RV64 default ABI semantics are retained. Native tests exercise mixed GP/SSE classes and register/stack overflow, plus calls into the real glibc `vsnprintf`.

**Typed `va_arg` instead of incorrect RV64 cursor arithmetic:** A new target-neutral Core instruction `VARIADIC_NEXT_SCALAR` is **appended** to the stable enum (old opcode numbering preserved). Only x86-64 lowering produces this instruction; its emitter reads/updates the System V register-bank offsets or overflow-stack pointer according to the requested scalar type. RV64 retains its existing direct eight-byte pointer cursor and explicitly rejects the x86-only opcode. Test `varargs_read` iterates over **seven integer plus nine floating variadic arguments**, intentionally crossing both x86 register banks into stack overflow; native result is 42.

**Explicit limitation:** `va_copy` is still **fail-closed** on x86: the shared frontend's current pointer representation would otherwise alias the two independently mutable System V cursors. Proper destination-owned 24-byte cursor copying has **not** been implemented. No arbitrary by-value aggregate or long-double varargs ABI is claimed.

**Full zlib native acceptance SUCCESS:** [Actions #39114118494](https://github.com/yituanxing/minic-toolchain/actions/runs/39114118494) first confirmed all **15/15 zlib v1.3.1 C translation units** produced x86-64 machine objects, GNU-linked against unmodified upstream `test/example.c`, and executed with **GCC byte-exact stdout/stderr, exit=0**. Latest [Actions #39114745675](https://github.com/yituanxing/minic-toolchain/actions/runs/39114745675) additionally built **upstream `test/minigzip.c`** as GCC and MiniC-library native executables from the same untouched pinned zlib source, compared compressed gzip byte streams exactly, and confirmed **reciprocal GCC→MiniC and MiniC→GCC decompression** of a binary input with embedded NUL/high-bit bytes. All passed. Existing native tiny-AES/cJSON GCC differential, x86 ABI/ELF, and RV64/AR/LD focused regressions passed in the same run.

**Lua 5.5 current frontier:** Lua's unmodified GCC reference build passed; MiniC now compiles the first TU, `lapi.c` (which originally failed on the variadic cursor). The next `lcode.c` fails at preprocessed `math.h` line **290:30**, on an actual system-header declaration `extern int __fpclassifyf128(_Float128 __value)` (`expected type name`). The target-independent frontend/type system does not yet fully recognize the x86 GNU `_Float128` extension. This is **not** a justification to silently equate _Float128 with 80-bit x86 long double, fabricate system header prototypes, or bypass real Lua source. Study target type semantics and full actual usage before admitting that extension. **Lua full native build/runtime is not claimed.**

### Post-ABI architecture checkpoint
- Real-project evidence supports reusing shared Core call/effect representation and the new *typed* variadic read, while allocating concrete GP/SSE register banks, register-save area and stack arguments in the x86 target.
- `RV64` variadic cursor and x86 System V `__va_list_tag` are deliberately **not** folded into one generic pointer/ABI allocator.
- **Avoid premature Machine IR, target-independent spill allocation, broad backend refactoring, or a speculative `_Float128` alias.**
- The changed shared `core_ir.h`, `core_ir.c`, `core_lower.c` and RV64 opcode switch coverage have passed **focused** RV64 regression, but the exact new candidate still needs a **fresh full Linux 2064/GNU link/QEMU P1** before merge.

## Final current-source RV64 seven-way Linux requalification (2026-10-18)

The formerly outstanding shared Parser/Core change gate is now **completed and
successful** on [GitHub Actions #39115412186](https://github.com/yituanxing/minic-toolchain/actions/runs/39115412186).

- Built current multi-target MiniC from the feature candidate source with the
  historically certified `MINIC_LINUX_FULL_CROSSBLOCK=1` and
  `MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1` profile.
- **Seven independent producers all passed** with freshly compiled Linux
  6.6.143 RV64 C objects: `295+295+295+295+295+295+294 = 2064/2064`.
  `MINIC_NATIVE_PROFILE=PASS`, `COHORT_COMPILE=PASS`,
  `COHORT_ABI=PASS`, and `COHORT_PRODUCE_ONLY=PASS` were emitted per shard.
- Receiver independently rechecked the seven newly built source-identified
  object groups: `GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`,
  `COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`,
  `COHORT_ABI=PASS objects=2064`.
- Original GNU linker built the kernel from the verified objects:
  `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`.
  This `REUSED` marker refers to **freshly produced same-run object union**,
  not old compiler binary or recycled code from a previous revision.
- **QEMU P1 12/12 passed**:
  `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`,
  and `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`.

The old manifest entries for changed `src/core/core_lower.c` and
`src/target/riscv64/core_codegen.c` were refreshed to actual current
Git blob identifiers `05089643994b35ab32518c2f8441415c38d47144` and
`153221d487eea6fdb1c0178c25eb7c0da35077cc` respectively; this
authenticates exact input source identities, and the above independent
execution logs supply the actual correctness evidence. All other frozen
manifest entries remained unchanged.

The original `.github/workflows/linux-runtime-gcc-baseline.yml` on main has
an unrelated pre-existing malformed YAML/duplicate diagnostics block. For
this qualification only, the feature ref temporarily repaired that legacy
dispatch and restored **the original workflow byte-for-byte** once all
seven producers were launched and verified. The evidence is tied to immutable
Actions run SHA `c416ba7e70fc453b82fdbcf6658ebe3b67cf1e40`,
whereas the production PR tree carries no CI workflow changes.

**Scope limit:** This certifies source-built **RV64 MiniC**, GNU final link
and QEMU P1 on the final shared Parser/Core/RV64 source changes. It does
**not** certify resumed Stage4 fixed-point self-hosting, x86-64 Linux
kernel compile, nine-tool native union, complete x86 SysV aggregate ABI,
Lua runtime, or x86 `va_copy` correctness. Those remain separate.


## Lua 5.5.0 — complete native x86-64 toolchain milestone (2026-10-18)

**Actual release / source**: unchanged official Lua 5.5.0 SHA-256-verified
tarball and all 33 upstream C translation units. Genuine Ubuntu x86-64 system
headers are preprocessed by GNU; MiniC's selected x86-64 frontend/Core/backend
produces GNU x86 assembly; GNU assembles and links a **real Lua interpreter**.
No synthetic Lua source, fake prototypes, host GCC object substitutes, x86
MiniAS/MiniLD claim, or LLVM/GCC fallback compiler for a TU.

**Full real-project evidence**:
- [Focused run #39121428738](https://github.com/yituanxing/minic-toolchain/actions/runs/39121428738)
  logged `LUA_X86_64_FRONTIER compiler=33 assembled=33 total=33 blocked=0 scan_all=1`.
  Independent tiny-AES/cJSON/zlib 15/15 native regression and RV64 focused
  toolchain tests passed on the same feature source. This verifies 33 original
  Lua objects, **not** by itself interpreter linking/runtime.
- [Focused run #39121674706](https://github.com/yituanxing/minic-toolchain/actions/runs/39121674706)
  verified `LUA_X86_64_FRONTIER compiler=33 assembled=33 total=33 blocked=0 scan_all=1`,
  `LUA_X86_64_LINK=PASS objects=33 driver=GNU`, and
  `LUA_X86_64_RUNTIME=PASS oracle=GCC byte-exact sample=table,closure,coroutine,math,string`.
  A native Lua interpreter linked from **all 33 MiniC objects**, executed a
  deterministic Lua program and matched a separate pristine upstream GCC-built
  Lua executable byte-for-byte in stdout/stderr; `LUA_PROBE_EXIT=0`.
  This is a **real running Lua interpreter**, but not a full upstream Lua
  test-suite certification or self-hosting. The existing x86 focused tests
  also passed and `ZLIB_PROBE_EXIT=0`.

**Original project failures and precisely scoped source changes**:
1. Three original Lua zero-filled pointer globals with relocation-only
   initializers (`HOOKKEY`, `CLIBS`, `progname`) exposed x86 static data
   emission gap. `x86_emit_zero_relocation_global` reuses the **existing shared**
   `minic_data_layout_global_relocation_offset` for validated storage offsets,
   leaving symbol syntax in the x86 backend. Also tested standalone
   `lua_pointer_globals` native exit 42.
2. Six Lua TUs were blocked by *real glibc* `math.h` prototypes involving
   IEEE `_Float128`. Shared frontend/type verification and target DataLayout
   now carry **distinct** x86 `_Float128` identity/16-byte size and alignment,
   without aliasing it to x86 80-bit `long double`; RV64 still rejects
   this target-specific spelling. The actual project currently requires
   prototype declarations, **not** general binary128 arithmetic or ABI calls.
3. Lua `lvm.c` computed-goto tables reused Core's stable label statement
   IDs for ELF relocation targets; x86 added only `.Luser_<id>` target-specific
   emission and `jmp *%rax` selection for the **already-shared**
   `MINIC_CORE_TERMINATOR_INDIRECT_BRANCH`. The pointer table and indirect
   branch are exercised by a native compiled fixture.
4. Remaining Lua record globals with selected nested unions required a
   bounded recursive x86 aggregate initializer emitter. It uses shared
   `MinicDataLayout`, Core flattened initializer slots and existing
   union-selection metadata. This exposes a **future extraction candidate**
   shared with RV64's mature recursive constant emission: the neutral walk
   over aggregates/initializer slots may be shareable, whereas target
   directives, relocations, ABI and special bit-field cases remain target
   owned. Do **not** force a generic emitter on RV64 without separate
   differential proof. Native `lua_recursive_record` test passed.

**Validation and limitations**:
- All new x86 acceptance tests and **focused RV64 + AR + LD** regressions have
  passing immutable run IDs above. The Lua script's full mode explicitly
  opts in with `LUA_X86_64_SCAN_ALL=1`; standard mode remains fail-fast.
- Since shared `type.h/type.c`, `parser_type.c`, `ast_verifier.c`,
  and `target/data_layout.[ch]` changed **after** RV64 seven-way P1 receipt
  #39115412186, a **new full RV64 Linux 2064/2064 GNU link/QEMU P1**
  requalification on the final source remains a merge gate. Do not misattribute
  the earlier receipt to these new shared-source changes.
- x86 dynamic PIE and SysV `va_list` type-representation limitations,
  general binary128 arithmetic/call ABI, by-value aggregates, and x86
  self-hosting are **not** implied by the Lua sample passing. Keep this PR draft.
- The previously verified feature-only CI dispatch repairs were **temporary**.
  Before finalizing this branch they were restored exactly to the frozen main
  workflow blobs; the branch production diff must contain no workflow YAML
  changes. Historical CI runs are immutable and independently inspectable.

## New RV64 complete Linux acceptance after shared Lua-driven changes (2026-10-18)

**Immutable full run:** [#39123610218](https://github.com/yituanxing/minic-toolchain/actions/runs/39123610218), candidate Git commit
`6fe70245d241d4bb69db660f497529aa200552ce` (only the
**temporarily parseable, previously accepted verification YAML** differs from
the eventual production feature tree). Latest shared type, frontend parser,
AST verification, DataLayout and all RV64 compiler sources are identical to
the subsequently restored production tree.

- All **seven** independent actual Linux 6.6.143 Kbuild C-object producer jobs
  succeeded, each proving native compiler identity/ABI and fresh compilation
  (six groups of 295, one of 294 = **2064/2064**).
- Receiver successfully authenticated the same-run seven-shard union:
  `GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`,
  `COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`,
  `COHORT_ABI=PASS objects=2064`.
- GNU final link uses newly produced same-run verified objects, not an older
  binary: `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`.
- Original QEMU P1: `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`;
  `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`.
- Exact native source SHA identity manifest was checked before the run: all
  seven frozen manifest entries matched the current feature blob identities.
  Changed shared `_Float128`/DataLayout/parser source files were recompiled
  during fresh seven-shard compiler builds and thus covered by this execution.
- After success, the temporary Linux workflow was restored to main's exact
  Git blob `75744ca2ecc2c1b8b9bc745a82c28b339d5f4fbf`.
  The production PR contains no workflow modification.

**Scope:** Real RV64 MiniC compilation of 2064 Linux C files, GNU final
link, and 12/12 QEMU P1. This is not a new nine-tool self-hosting acceptance.
Independent x86 Lua full 33/33 original-TU compilation, native linking,
and runtime GCC differential continue to be recorded above. x86 SysV
aggregate ABI, PIE and `va_list` type correctness remain separate gates.


## Second real Lua executable: native luac bytecode compiler (2026-10-18)

**Independent real-project receipt:** [focused #39124253419](https://github.com/yituanxing/minic-toolchain/actions/runs/39124253419),
candidate source/temporary focused CI commit `6483c956e28a00f516ac010b28b5db7331b99607`.

- The actual pristine upstream Lua 5.5.0 `luac.c` was preprocessed with
  genuine GNU host system headers; compiled using MiniC
  `--target=x86_64-linux`, assembled by GNU, and linked with the **32
  MiniC-compiled original Lua library objects** (excluding `lua.c` to
  avoid duplicate `main`) plus `luac.o`. No GCC C object substitution.
- The resulting x86 native `luac-minic` processed a deterministic original
  Lua source script using closures, tables, metatables, byte packing,
  coroutines, protected errors and GC. Its **binary bytecode output was
  identical byte-for-byte** to that from pristine GCC-built `luac` for
  exactly the same input path. The resulting bytecode executed successfully
  with the MiniC-built native Lua interpreter and the output/error streams
  were identical to those of the GCC-built Lua runtime.
- CI log:
  `LUA_X86_64_LUAC=PASS original=luac.c compiler=MiniC source_bytecode_identical_to_GCC=true native_runtime=PASS`.
  Original Lua interpreter 33/33 object build/link/runtime differential,
  pinned native zlib, the x86 checks and focused RV64/AR/LD also remained
  green; `LUA_PROBE_EXIT=0`,
  `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.
- This follow-on changed **only the Lua test driver**, not frontend, Core,
  x86 or RV64 compiler source. Thus the independent fresh
  [RV64 full Linux/QEMU P1 #39123610218](https://github.com/yituanxing/minic-toolchain/actions/runs/39123610218)
  continues to cover the exact current compiler blobs.
- Temporary feature-only focused workflow was restored byte-for-byte to
  main's Git blob `cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.
  The production PR has no workflow diff.

### Proven reuse and candidate next abstraction

Real upstream tiny-AES, cJSON, zlib and now Lua/Luac have exercised the
shared C frontend and verifier, target-dependent DataLayout, Core call
signatures, Core source-label identity, indirect-branch semantics, flattened
global initialization and union-member selection. Both RV64 and x86 emitters
perform recursive aggregate layout traversal. **Potential future shared
semantic walker**: map (type, initializer slot, selected union branch,
target DataLayout) to abstract byte spans / padding / relocation slots.
Architecture-specific output callbacks should continue to own actual
assembler directives, relocation syntax, ISA and call ABI. Do not extract
this until a real project reproduces meaningful duplicated behavior with
both backends and has differential coverage, because RV64's production
emitter also handles special bit-fields, flexible array members, and
other cases currently absent from the bounded x86 emitter.

**Not certified:** entire upstream Lua test-suite, general x86 PIE,
true x86 `__builtin_va_list` type model, binary128 arithmetic/call ABI,
full aggregate SysV ABI, or x86 MiniAS/MiniLD/self-hosting.


## Real Lua ELF/PIE acceptance: target-owned GOT address materialization (2026-10-18)

**Candidate source and full actual focused receipt:**
[CI #39125162510](https://github.com/yituanxing/minic-toolchain/actions/runs/39125162510),
source SHA `5b039f62181442e5d5b3df11d5d4ce206d2b6867`.

**Original observed incompatibility:** x86 codegen `GLOBAL_ADDRESS` and
`FUNCTION_ADDRESS` emitted `leaq symbol(%rip),%rax`, generating
`R_X86_64_PC32`; this cannot address an interposable external libc symbol
such as `puts` in a default Linux PIE. A local GNU binutils GCC
reproduction confirmed the linker error for the PC32 form. In contrast
`movq symbol@GOTPCREL(%rip),%rax` worked for both `-pie` and
`-no-pie`, and local symbols are eligible for linker relaxation.

**Minimal targeted change:** Select GOT-based symbol address materialization
in `src/target/x86_64/codegen.c`; **no** shared parser, Core,
DataLayout or RISC-V source changes. Added a native test taking real glibc
`getenv` function and `environ` data-symbol addresses and running both
PIE/non-PIE binaries with exit 42.

**Real project not only synthetic smoke:**
- Same original upstream Lua 5.5.0 **33/33** C translation units compiled
  by MiniC and assembled by GNU.
- Those *same 33 MiniC objects* linked as native default Linux
  `-pie` interpreter and earlier `-no-pie` interpreter. Both executed
  the same deterministic Lua script with stdout/stderr byte-exact to
  the original GCC-built Lua interpreter.
- Upstream `luac.c` compiled with MiniC, GNU linked with its 32
  MiniC-built library TUs to separate `-pie` and `-no-pie` native
  bytecode compiler executables. Both produced bytecode files identical
  to upstream GCC `luac`; PIE Lua executed the PIE luac bytecode with
  byte-exact original runtime output.
- CI markers:
  `PASS: x86-64 GOT external function/object addresses PIE and non-PIE exit=42`;
  `LUA_X86_64_PIE=PASS objects=33 oracle=GCC runtime=byte-exact`;
  `LUA_X86_64_LUAC_PIE=PASS compiler=MiniC native_pie=true bytecode=GCC-identical`;
  `LUA_PROBE_EXIT=0`, `ZLIB_PROBE_EXIT=0`,
  `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.
- GNU CRT, assembler, linker, and libc remain intentional platform tools:
  this does not certify an x86 native MiniAS/MiniLD, a shared-object
  `-fPIC` toolchain, TLS, all ELF symbol preemption modes, or self-hosting.
- The most recent **fresh RV64 Linux 2064/2064 and QEMU P1 12/12**
  [#39123610218](https://github.com/yituanxing/minic-toolchain/actions/runs/39123610218)
  remains source-identical for its compiler-side shared/target inputs:
  the new functional source change affects **x86 codegen only**, and RV64
  focused tests passed again in #39125162510.

**Architecture decision:** GOT/PLT/symbol-preemption behavior is target ELF
machine emission, not a reason to generalize Core or invent a common ABI.
Preserve the already-shared semantic symbol references and DataLayout,
allow each backend to select correct target relocation expressions.
The potential shared recursive aggregate initializer walker remains
**only a candidate**, not yet implemented.
