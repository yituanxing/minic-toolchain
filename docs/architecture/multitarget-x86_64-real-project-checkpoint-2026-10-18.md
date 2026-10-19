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


## SysV x86 va_list C type identity safety and fresh RV64 certification (2026-10-18)

**Exact original ABI observation:** local native GCC x86-64 reports
`sizeof(__builtin_va_list) == 24`, `_Alignof(__builtin_va_list) == 8`,
and `__builtin_types_compatible_p(__builtin_va_list,void*) == 0`.
The compiler's earlier x86 lowering represented the builtin as `void*`,
whose `sizeof` is 8, although native variadic argument forwarding worked
using a privately allocated 24-byte SysV tag. That conflation could
**silently return the wrong size** for both typedef-based type queries and
local variable queries.

**Implemented bounded correctness guard:**
- Preserve the working x86 runtime one-pointer cursor transport and its
  private 24-byte mutable tag; assign it a **distinct internal type identity**
  `MINIC_TYPE_BASE_X86_VA_LIST_CURSOR` instead of `void*`.
  Preserve any temporary Core address-of proxy pointer layers in the type
  verifier; RV64 continues to use its validated legacy pointer model.
- `minic_target_info_sizeof_type` rejects direct x86 cursor-type
  `sizeof` rather than reporting 8. This covers
  `sizeof(__builtin_va_list)`, `sizeof(va_list)`, and
  `sizeof(local_va_list)`, even after a typedef; sizeof ordinary pointer
  types remains unchanged.
- A native x86 regression asserts the type is distinct from `void*`,
  verifies the two unsupported sizeof paths **fail closed with no emitted
  assembly**, and runs the compiled valid identity probe with exit 42.
  Existing `va_start/va_arg/va_end`, glibc forwarding and intentionally
  unsupported `va_copy` remain independently tested.
- **Not the final array model:** the internal proxy still uses eight bytes
  of compiler-managed cursor storage, while the genuine SysV type is a
  one-element array of a 24-byte tag. This guard does not claim correct C
  array-of-va_list layouts, copies, all initializer/assignment semantics,
  or general `va_copy`. Those require a genuine array-of-record
  storage and argument decay model, and must be driven by an actual
  workload with new native GCC-differential tests before merging.

**Proven current source:** [focused run #39126567409](https://github.com/yituanxing/minic-toolchain/actions/runs/39126567409)
SUCCESS, including native x86 varargs forwarding, native Lua 33/33 original
objects, both PIE/non-PIE interpreter and luac execution/GCC byte-for-byte
differentials, zlib and independent focused RV64/AR/LD checks.
[Fresh full 7-way RV64 Linux/QEMU run #39126712399](https://github.com/yituanxing/minic-toolchain/actions/runs/39126712399)
SUCCESS on the new shared type/Parser/verifier/TargetInfo implementation
(committed as `f4545f8dd6299d545b0e6c1355f8f45b92a6d530` with
**temporary verification-only workflow configuration**). Actual logs:
`GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`,
`COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`,
`COHORT_ABI=PASS objects=2064`,
`COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`,
`COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`,
`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`.

All seven Kbuild shards were freshly compiled by the same current RV64
compiler source, then their verified same-run objects were reused by GNU
link and QEMU. This is not a previous Linux image, Stage4 self-hosting,
or a native x86 assembler/linker qualification.

**CI hygiene:** the original Linux and focused workflow files were
restored to the exact same Git blobs as `main`, respectively
`75744ca2ecc2c1b8b9bc745a82c28b339d5f4fbf` and
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`; no workflow changes
remain in the production PR. The earlier malformed main workflow
can still produce zero-job status failures independently of compiler behavior.
Keep PR #524 Draft until remaining source and supported ABI contract gates
are consciously resolved.


## SQLite 3.54.1 — new native x86 real-project frontier (2026-10-18)

**Project selection:** New, substantially larger and more demanding real C
workload after original upstream tiny-AES/cJSON/zlib/Lua/Luac: the official
[SQLite download](https://www.sqlite.org/download.html) version **3.54.1**
(released 2026-10-16), with pristine amalgamation `sqlite3.c` and `sqlite3.h`.
The exact downloaded ZIP is SHA3-256 authenticated against the **publisher's**
hash `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`.
No SQLite source rewrites, fake C declarations, locally invented source files
substituted for the project, or GCC C object fallback.

**Reusable project harness:** `tests/external/sqlite/probe-x86_64.sh`,
`make check-x86_64-sqlite-frontier`. The harness uses unmodified SQLite
with `SQLITE_THREADSAFE=0` and `SQLITE_OMIT_LOAD_EXTENSION` on a native
x86-64 Ubuntu/glibc environment. It verifies the upstream SHA3, preprocesses
with original system headers, then compiles the **same preprocessed C**
through MiniC x86-64. Discovery mode is opt-in:
```sh
SQLITE_X86_64_DISCOVERY=1 make MODE=release CFLAGS=-Werror \
    BUILD_DIR=build/multitarget check-x86_64-sqlite-frontier
```
**Important:** a compiler `SQLITE_X86_64_FRONTIER=BLOCKED` is an **expected
and clearly labelled incomplete observation** in discovery mode; the script
returns zero only so that unrelated legacy RV64/AR/LD verdicts still run. This
is **not SQLite pass**. Ordinary (non-discovery) execution returns nonzero on
the first failure. A valid `SQLITE_X86_64_RUNTIME=PASS` requires a full
MiniC-compiled `sqlite3.o`, native GNU link, independent GCC build from
the exact same preprocessed TU, and byte-exact SQL workload including
transactions, prepared statements, B-tree index, rollback, and integrity check.

**Actual original-source progression:**
- [First CI #39128010038](https://github.com/yituanxing/minic-toolchain/actions/runs/39128010038):
  upstream ZIP hash passed; genuine preprocessed SQLite **93,168 lines**,
  **2,705,806 bytes**. First blocker `sqlite3.i:1370`: glibc
  `extern _Float32 strtof32(...)`. SQLite source was unmodified.
- [Follow-up focused #39128364679](https://github.com/yituanxing/minic-toolchain/actions/runs/39128364679):
  new shared extended floating type identity model passed native GCC
  differential semantic probes; frontier advanced to `sqlite3.i:1555`
  `__builtin_bswap16` from glibc `__bswap_16`.
- [Current focused #39128758993](https://github.com/yituanxing/minic-toolchain/actions/runs/39128758993):
  parser/Core byte-swap support passed an x86 native test including
  `__builtin_bswap16/32/64` mixed widths and **single evaluation** of
  `a[i++]`. SQLite frontier advanced to `sqlite3.i:6640`:
  `__atomic_store_n((&mem0.nearlyFull),(n>0 && n<=nUsed),0)`.
  Real Lua 33/33 object and PIE/non-PIE interpreter/luac/GCC-bytecode
  tests, pinned zlib and existing RV64/AR/LD focused suite also passed.
  `SQLITE_X86_64_FRONTIER=BLOCKED at=MiniC (not a pass)`.
  Because these errors occur early during parsing, the observed
  `maxrss_kb=6172/6308/10984` and near-zero elapsed values **are not**
  meaningful full SQLite compiler performance benchmarks.

**Shared semantics exposed by this real project:**
- Real glibc headers need **distinct** `_Float32`, `_Float64`,
  `_Float32x` and `_Float64x` C type identities, including sizes
  4/8/8/16 and alignments 4/8/8/16 for the selected x86 target;
  GCC `__builtin_types_compatible_p` proves these are **not** the
  same types as `float`, `double`, `double`, `long double`,
  respectively. Shared `MinicType`, parser, AST verifier and
  DataLayout now distinguish these. Actual extended-FP arithmetic
  and call ABI remain unsupported; their declarations and type
  queries do not imply runtime binary FP support. RV64 continues to
  reject these x86-specific spellings rather than guessing its ABI.
- Real glibc `__builtin_bswap16/32/64` demands correct width and
  **exactly one operand evaluation**. Shared parser and Core Lowering
  reuse existing Core shift/mask/OR operators, without target-specific
  x86-only hacks or changing the mature RV64 emitter. Current lowering
  prioritizes correctness; if a later SQLite runtime hotspot reveals
  code-size or speed issues, add target-native `bswap` selection
  **after** source-level profiling rather than speculative optimization.

**RV64 full-source requalification:** The above shared changes modify
`src/frontend/ast.h` and `src/core/core_lower.c` identities.
The strict source SHA manifest `docs/ci/native-runtime44-perf4-validated-sha1.txt`
was refreshed for those **two exact blobs only**. SHA identity alone
is not evidence of correctness. Fresh
[seven-shard Linux/QEMU CI #39129097859](https://github.com/yituanxing/minic-toolchain/actions/runs/39129097859)
**SUCCESS** on the exact current compiler source:
`GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`,
`COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`,
`COHORT_ABI=PASS objects=2064`,
`COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`,
`COHORT_P1=PASS syscalls=12 python_era_oracle=PASS`,
`COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`.
The Linux receiver reuses **newly compiled, same-run** 2064 verified objects
for GNU final link and QEMU P1, not an old image.
No source files were changed after this full source-qualified run;
the two temporary testing workflow edits were restored **exactly** to
main's Git blobs `75744ca2` and `cd9a880a`. No workflow YAML is
intended in the PR production diff. The malformed legacy main YAML still
causes unrelated zero-job push checks.

**Next gate:** `__atomic_store_n` is a genuine atomic-memory-operation
feature, not a generic assignment. Model a target-neutral Core atomic
store/load with explicit width, alignment, and requested memory order;
then select target-correct atomic instructions/barriers for x86 and RV64,
or fail closed on unsupported orders/types. The first observed SQLite
store requests relaxed order (`0`), but the implementation must not
silently generalize to all memory orders or assume thread-unsafe compile
flags make atomic language semantics disappear. Use independent native
GCC-differential atomic tests and the upstream SQLite frontier before
adding it, and re-run appropriate RV64 gates on shared-source changes.
**SQLite database runtime remains unverified until it actually links and
executes its genuine SQL operations.**


## SQLite atomic read/write semantic gate (2026-10-18)

**New observed original-source need:** the pinned official pristine
SQLite 3.54.1 `sqlite3.c` fails at `sqlite3.i:6640`,
`__atomic_store_n((&mem0.nearlyFull),(n>0 && n<=nUsed),0)`.
The immediately subsequent real read is `sqlite3.i:6681`,
`__atomic_load_n((&mem0.nearlyFull),0)`. `0` means GNU relaxed
memory order. Neither is equivalent in language semantics to a generic,
possibly reordered or elided ordinary C assignment/load.

**Actual implementation:**
- `MINIC_EXPRESSION_BUILTIN_ATOMIC_STORE` and
  `MINIC_EXPRESSION_BUILTIN_ATOMIC_LOAD` are distinct AST nodes with
  operand references, type/width and explicit order; traversal and verifier
  validate their shape. Address and value expressions are evaluated once.
- `MINIC_CORE_INSTRUCTION_ATOMIC_STORE` is a memory effect and
  `MINIC_CORE_INSTRUCTION_ATOMIC_LOAD` produces a typed SSA value.
  Core verification checks address/pointee and integer type identity.
  Keeping these separate from ordinary `LOAD`/`STORE` prevents their
  accidental semantic downgrade by future optimizations.
- For x86-64, naturally aligned **1,2,4,8-byte integer** relaxed
  operations lower to single scalar MOV accesses. This is the supported
  aligned x86-64 atomic subset; no misleading promise of arbitrary
  unaligned atomicity. Other memory orders, including seq_cst (5),
  fail closed at the frontend. Full acquire/release, fences, RMW and
  compare-exchange remain unimplemented.
- RV64 backend explicitly recognizes the new Core opcode identities and
  traverses their SSA uses, but **rejects actual atomic emission** until
  its correct RVWMO/A-extension behavior is implemented; no ordinary
  RV64 loads/stores are substituted for atomic memory operations.
- The existing x86 native test suite exercises actual valid relaxed
  stores/loads with wide integers, volatile memory, operand
  single-evaluation, non-PIE GNU linkage, and rejects stronger-order
  tests without emitting assembly.

**Immutable real project receipts:**
- [Focused #39130830980](https://github.com/yituanxing/minic-toolchain/actions/runs/39130830980)
  SUCCESS with atomic-store, Lua 33/33 interpreter and `luac`,
  original zlib and RV64 owner tests. SQLite advanced to
  `sqlite3.i:6681` where `__atomic_load_n` was not yet implemented.
- [Focused #39131255903](https://github.com/yituanxing/minic-toolchain/actions/runs/39131255903)
  SUCCESS with paired
  `PASS: x86-64 sqlite_atomic_store exit=42` and
  `PASS: x86-64 sqlite_atomic_load exit=42`, plus negative
  unsupported memory-order checks, zlib/Lua and RV64 focused.
  SQLite advanced to `sqlite3.i:10004` with the next concrete blocker
  `return (double)(__builtin_inff());`. SQLite full translation
  unit/runtime remains **BLOCKED**, not passed.
- [Full Linux #39131498898](https://github.com/yituanxing/minic-toolchain/actions/runs/39131498898)
  SUCCESS on exact new shared AST/Core/IR and RV64 fail-close code:
  `GNU_MINIC_SHARD_MERGE=PASS objects=2064 shards=7`,
  `COHORT_SCOPE=FULL_C_UNIVERSE objects=2064`,
  `COHORT_ABI=PASS objects=2064`,
  `COHORT_LINK=REUSED name=full_all_verified selection=prefix:2064`,
  `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`,
  `COHORT_RESULT=PASS candidate_objects=2064 scope=FULL_C_UNIVERSE same_config=true`.
  The 2064 objects were produced on the **same current run's** seven
  independent workers, GNU-linked and booted in QEMU; not old cached binaries.
  The frozen SHA manifest was updated for exact changed
  `src/frontend/ast.h`, `src/core/core_lower.c`, and
  `src/target/riscv64/core_codegen.c`, then verified in that run.
  Manifest SHA matches are not a substitute for runtime tests.

**Scope/decision:** This gives shared atomic *semantic representation*
between backends while explicitly avoiding an unqualified RV64 emission.
Continue the real upstream SQLite source from its next `__builtin_inff`
blocker. Do not rush generalized atomic architecture, silently broaden
memory-order guarantees, fabricate SQLite coverage, or change the original
SQLite source. Both temporary focused and Linux workflow configurations
were restored byte-for-byte to the original `main` versions after the
immutable successful runs, leaving no workflow YAML diff in PR #524.


## SQLite x86-64 real-source checkpoints after relaxed atomics (2026-10-18)

Pristine upstream SQLite 3.54.1 remains verified by SHA3-256
`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`;
the immutable preprocessed TU is 93,168 lines. Discovery mode is explicitly
**not** a compiler success, even when the CI owner job is green.

- [#39135606151](https://github.com/yituanxing/minic-toolchain/actions/runs/39135606151):
  GNU `__builtin_inff`, `__builtin_inf`, and `__builtin_huge_val` verified by
  x86 native tests; SQLite advanced to line 86,538 at relaxed stores to function
  and object pointers.
- [#39136016221](https://github.com/yituanxing/minic-toolchain/actions/runs/39136016221):
  frontend, AST verifier, Core lowering, Core IR verifier and x86 emission
  accepted typed relaxed atomic loads/stores of pointer objects, including
  callable function pointers. Real SQLite then exposed a Core verification failure
  inside `sqlite3_soft_heap_limit64`.
- [#39136352121](https://github.com/yituanxing/minic-toolchain/actions/runs/39136352121):
  opt-in Core instruction verifier diagnostics identified an atomic store
  at line 6,640 with a cross-basic-block operand availability violation.
  The earlier frontend blocker had masked this independently existing issue.
- [#39136726398](https://github.com/yituanxing/minic-toolchain/actions/runs/39136726398):
  reuse the ordinary scalar assignment spill/reload seam for relaxed atomic
  stores across short-circuit CFG. Native short-circuit and pointer tests passed.
  Real SQLite proceeded to `sqlite3Config`, a nested initialized record global.
- [#39137004013](https://github.com/yituanxing/minic-toolchain/actions/runs/39137004013):
  reuse the existing x86 recursive global-initializer emitter for nested records;
  `sqlite3Config` cleared. The next blocker, `aXformType`, was an array of
  records with embedded character arrays.
- [#39137193731](https://github.com/yituanxing/minic-toolchain/actions/runs/39137193731):
  recurse through arrays of records with extra flattened initializer slots;
  `aXformType` cleared. The next blocker was SQLite's `zeroItem`.
- [#39137426742](https://github.com/yituanxing/minic-toolchain/actions/runs/39137426742):
  relocation-free, all-zero record initializers now emit the exact DataLayout
  storage size as zeros, with an independent bit-field/nested-field regression.
  Real SQLite passed this global-emission gate and reached x86 instruction
  selection: `x86-64 Core instruction unsupported: function sqlite3RunParser, kind 47`.
  Focused x86, Lua 33/33 + native luac, zlib, and independent RV64 regressions
  succeeded; SQLite **full object and SQL runtime remain BLOCKED**.

`kind 47` is `MINIC_CORE_INSTRUCTION_CALL`, not an unknown Core opcode.
The x86 backend already emits ordinary scalar calls. Its direct-call path
currently rejects aggregate result objects, and its argument dispatcher rejects
object-form arguments. Next investigate the **exact failing callee and argument
or result class** in `sqlite3RunParser`, then implement only the required
SysV x86-64 ABI classification/placement with native GCC-differential tests.
Do not infer that record ABI is the exact cause without the targeted trace.
Neither fake SQLite modifications nor broad ABI assumptions are acceptable.
For every shared Core source change, qualify RV64 with an appropriately scoped
oracle; the most recent full Linux 2064/QEMU P1 result predates these changes.
The temporary focused CI workflow was restored byte-for-byte to `main`.


## SQLite x86 ABI, integer primitives and 128-bit gate (2026-10-18)

All receipts below compile the **unchanged** official SQLite 3.54.1
amalgamation (93,168 preprocessed lines, pinned SHA3-256 already above).
Focused CI success is **not** SQLite full compile/object/runtime success.

- [#39138001163](https://github.com/yituanxing/minic-toolchain/actions/runs/39138001163):
  opt-in x86 call diagnostics exposed `sqlite3RunParser` -> `sqlite3Parser`
  with two scalar VALUE arguments and a third record OBJECT parameter.
  `Token` has a pointer at byte 0 and u32 at byte 8 (size 16).
- [#39138493433](https://github.com/yituanxing/minic-toolchain/actions/runs/39138493433):
  x86 SysV two-INTEGER-eightbyte record calling subset passed:
  MiniC-to-MiniC, GNU caller -> MiniC callee, MiniC caller -> GNU callee,
  and whole-object stack fallback after six GPR arguments. The exact
  supported record shape is deliberately bounded; there is no claim of
  arbitrary SysV aggregate classification. Real SQLite advanced to
  `sqlite3LogEst`, Core kind 22, `INTEGER_CLZ`.
- [#39138706284](https://github.com/yituanxing/minic-toolchain/actions/runs/39138706284):
  x86 CLZ/CTZ for typed unsigned 8/16/32/64-bit Core values via baseline
  BSR/BSF (no LZCNT/BMI requirement) passed native boundary tests. Real
  SQLite advanced to `sqlite3AddInt64` and kind 15, INTEGER_OVERFLOW.
- [#39138951771](https://github.com/yituanxing/minic-toolchain/actions/runs/39138951771):
  checked add/subtract/multiply for matched 32/64-bit result types, signed
  and unsigned, with stored result and overflow boolean from target flags,
  passed the native overflow cases. Real SQLite advanced to
  `sqlite3Multiply128`, Core kind 16, INTEGER_CONVERSION, requiring
  `__uint128_t` semantics. Lua 33/33/luac, original zlib, and independent
  RV64 scoped suites continue PASS in the same focused run.

**Current new architecture boundary:** SQLite's `src/util.c` uses
`__uint128_t` for `sqlite3Multiply128` and `sqlite3Multiply160`:
unsigned 64->128 conversion, 128-bit multiplication, addition, shifts and
truncation back to 64 bits. x86 M1 uses **one 8-byte frame slot for each
Core scalar value**; an isolated acceptance of kind 16 would silently
truncate the upper limb. Do not implement it that way. Next design typed
two-limb value storage/reloads and the corresponding Core op lowering in
x86, with GCC-differential tests for nonzero high halves and crossing
64-bit boundaries before retrying original SQLite. Preserve target-neutral
Core representation; keep RV64 logic and x86 ABI distinct.

The temporary focused CI workflow is restored exactly to the main blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`. Neither SQLite full
object nor database transactions/index/rollback/integrity tests have passed.
This latest x86-only extension does not replace a newly qualified full
RV64 Linux 2064/QEMU P1 run on the updated branch.


## SQLite 3.54.1 full x86-64 native runtime gate (2026-10-18)

The next real-project iteration implemented backend-local **typed Core value
slots**, 8 bytes for ordinary scalars and **16 bytes only for int128**, with
an offset lookup replacing the old fixed `value_id * 8` assumption.
This avoids doubling all x86 scalar stack slots or changing the shared
Core IR representation. 64/128 integer conversions, two-limb memory
transport, add/subtract/multiply, bitwise operations, shifts across the
64-bit boundary and two-register x86-64 SysV `RDX:RAX` return transport
are handled in `src/target/x86_64/codegen.c`.

- [#39140782241](https://github.com/yituanxing/minic-toolchain/actions/runs/39140782241)
  is the **first true SQLite full success** after replacing the previous
  `sqlite3Multiply128` 128-bit conversion blocker. It reports:
  `SQLITE_X86_64_SOURCE=PASS`,
  `INPUT=PASS version=3.54.1 preprocessed_lines=93168`,
  `COMPILER=PASS`,
  `OBJECT=PASS`,
  and `RUNTIME=PASS` with GCC byte-exact SQL output covering
  transactions, indexing, prepared statements, rollback, and
  `integrity_check`.
  The same scoped CI reports `TOOLCHAIN_FOCUSED_T1=PASS`,
  eight independent owner checks (RV64, archive/linker etc.), and
  Lua 33/33/luac plus original pinned zlib.
- [#39141008797](https://github.com/yituanxing/minic-toolchain/actions/runs/39141008797)
  reconfirms the full SQLite native object/runtime/oracle after expanded
  tests. Native MiniC and GNU oracle both return expected 42 for two-limb
  arithmetic and signed/unsigned shifts; GCC caller -> MiniC callee and
  MiniC caller -> GCC callee both correctly transport a `__uint128_t`
  return value with nonzero high and low 64-bit halves. All eight
  independent scoped regression checks again pass.

**Scope and exclusions:** this proves a SQLite-pinned x86-64 path, not
arbitrary GNU C semantics or every SysV ABI classification. General
128-bit *parameter* passing, 128-bit division/remainder, arbitrary
aggregate register classes, 128-bit relational comparisons, and nontrivial
aggregate returns remain unqualified and should continue to fail closed.
The x86 backend has a deliberately restricted two-INTEGER eightbyte
record-call subset for SQLite's Token; it does not implement general
SysV structure classification. Linux 2064/QEMU P1 was not rerun on this
new x86-only code branch: the previously verified full Linux result
predates it, while focused RV64 checks have been revalidated.

**Next project-driven step:** checkpoint the full SQLite runtime green
status as a durable test oracle; select the next unchanged real project
whose feature needs exercise a genuinely missing shared or backend seam.
Do not refactor to a hypothetical generalized multi-backend pass before
a concrete differential failure. Always reuse focused regressions and
restore the temporary workflow to main after the measurement.

## TinyCC 0.9.27 real-program pressure checkpoint

The next frozen upstream project is **TinyCC 0.9.27**, pinned to
`TinyCC/tinycc@d348a9a51d32cece842b7885d27a411436d7887b`.
`tests/external/tinycc/probe-x86_64.sh` verifies original upstream file
Git hashes, runs original `./configure` and a full GCC baseline `make tcc`,
then preprocesses and compiles the original 10-TU x86_64 target with MiniC.
No modified TinyCC source or GCC-substituted MiniC objects. **Discovery mode
reports blockers rather than qualifying a full TinyCC build.**

- [#39141675330](https://github.com/yituanxing/minic-toolchain/actions/runs/39141675330)
  confirmed original `tcc.c` (5,746 preprocessed lines) compiled into a
  MiniC-produced x86 object, with the pristine GCC-built TCC baseline.
- [#39141937095](https://github.com/yituanxing/minic-toolchain/actions/runs/39141937095)
  expanded discovery to all 10 TUs: **7/10 objects passed**, and distinct
  real blockers were `tccpp.c` (call ABI), `tccgen.c` (mixed tentative
  declarations), and `x86_64-gen.c` (GNU predefined function spelling,
  followed by dynamic Core stack allocation).
- [#39142580053](https://github.com/yituanxing/minic-toolchain/actions/runs/39142580053)
  validated mixed global array/pointer declaration regression and
  `__PRETTY_FUNCTION__` lexical-name support; source-driven TinyCC moved
  into more specific errors.
- [#39142873168](https://github.com/yituanxing/minic-toolchain/actions/runs/39142873168)
  **SUCCESS for focused CI and independent regression owners**,
  validates new x86 `DYNAMIC_STACK_ALLOC` native test exit=42 and moves
  original TinyCC to **8/10 compiled + assembled objects**. The newly
  passing original TU is `x86_64-gen.c`. The current precise blockers:
  `tccpp.c::parse_number` direct call `__extenddftf2`, requiring a
  correctly represented x86-64 long-double return ABI; and
  `tccgen.c` at preprocessed line 5269,
  `} *cur_switch;`, a record definition plus pointer declarator.
  The x86 long-double ABI must **not** be silently treated as binary64.
  The record-declarator parser issue should be solved using existing
  general declaration handling, not a TinyCC-specific rewrite.

The same #39142873168 job confirms unchanged original SQLite 3.54.1 full
MiniC x86 object and GCC byte-exact SQL runtime, Lua, zlib and eight
independent focused RV64/AR/LD checks. **TinyCC has NOT yet linked or
executed a fully MiniC-built compiler**, and a fresh full RV64 Linux
2064/QEMU P1 has not been re-run on this source.

The temporary focused workflow is restored byte-for-byte to main
(`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`), preserving no
workflow changes in the draft PR. The legacy unrelated broken baseline
workflow is not a source-code failure qualification.

## TinyCC original compiler + functional execution checkpoint — 2026-10-19

**Original TinyCC 0.9.27 x86-64 native path now qualifies for the
specified 10-C-unit compile, GNU assembly/link, command-line version
execution and one real consumer C program, with independent GCC oracles.**
It does NOT qualify TinyCC full self-hosting or its complete upstream test
suite. The frozen pristine input remains TinyCC commit
`d348a9a51d32cece842b7885d27a411436d7887b` and the first-unit
hashes are checked by the regression script.

- [Focused CI #39147169195](https://github.com/yituanxing/minic-toolchain/actions/runs/39147169195):
  `TINYCC_X86_64_OBJECTS=PASS count=10`,
  `TINYCC_X86_64_LINK=PASS objects=10 GNU_CRT=1`,
  `TINYCC_X86_64_RUNTIME=PASS executable=tcc version_oracle=GCC`.
  No host-GCC-produced TinyCC objects were substituted for the 10
  MiniC-produced compiler objects.
- [Extended CI #39147448590](https://github.com/yituanxing/minic-toolchain/actions/runs/39147448590):
  additionally proves `TINYCC_X86_64_CONSUMER=PASS backend=minic-tcc exit=42`,
  with `backend=gcc-tcc exit=42` and `backend=gnu exit=42` controls.
  The generated `tcc-minic` *actually compiled* a new unmodified C
  consumer source to an ELF relocatable `.o`. GNU linked that object
  to an independent host-compiled driver; the resulting executable
  returned 42. Log contains `TINYCC_X86_64_FUNCTIONAL=PASS`.
- Both CI jobs retain the full original SQLite 3.54.1 GCC byte-exact
  database runtime oracle and `TOOLCHAIN_FOCUSED_T1=PASS` for eight
  independently owned RV64, MiniAR and MiniLD focused regression checks.

The genuinely project-driven shared and backend improvements include
GNU `__PRETTY_FUNCTION__` in C, mixed tentative file-scope array/pointer
declarations, `struct {...} *object` parsing, x86 dynamic alloca with
SysV call alignment, and an x87 80-bit ABI (16-byte slots, st(0) return,
stack-passed argument values, load/store, selected typed Core conversion
helpers, signed/unsigned 64-bit boundary tests). The Core long-double
literal path is **still initially parsed at host binary64 precision**
and converted to a wider representation; despite the x87 machine format,
this is NOT proof of all 80-bit-source-literal precision. Also not yet
qualified: full long-double and complex libgcc semantics, all x86 SysV
aggregate classes, complete TinyCC upstream tests or self-hosting, and
a fresh 2064-file RV64 Linux/QEMU P1 at this HEAD. Keep these boundaries.

The temporary focused workflow modification used only to run these
checks is restored to the main workflow blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56` afterward. No
new workflow was introduced. The separate legacy
`linux-runtime-gcc-baseline.yml` CI failure remains unrelated and
is not counted as a green check.

**Next if extending this project:** first use the now-executable
MiniC-produced TinyCC to run a broader deterministic subset of upstream
compiler tests and compare against a GCC-produced TinyCC executable.
Prioritize observed semantics or runtime divergence, not hypothetical
cross-target abstractions.

## Original TinyCC tests2 + BusyBox 1.36.1 pressure checkpoint — 2026-10-19

**Scoped source and CI evidence, without modifying either upstream codebase.**

- [Focused CI #39148953666](https://github.com/yituanxing/minic-toolchain/actions/runs/39148953666)
  ended **success** with `TOOLCHAIN_FOCUSED_T1=PASS` and eight
  independent RV64/MiniAR/MiniLD checks.
- BusyBox pinned to the exact **1.36.1** release commit
  `mirror/busybox@1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4`;
  original Makefile, `coreutils/cat.c`, `coreutils/echo.c` Git blobs
  are checked. The probe runs upstream `make allnoconfig`, enables only
  `CAT` and `ECHO` in generated `.config`, then uses original
  Kconfig/Kbuild to build and run the native GCC applet baseline. These
  are configuration choices, **not source patches**.
- Both untouched BusyBox TUs preprocess and compile with MiniC x86-64
  (4,731 and 4,776 preprocessed lines respectively) and GNU assemble
  into genuine MiniC objects: `BUSYBOX_X86_64_TU=PASS` twice,
  `BUSYBOX_X86_64_FRONTIER=SCOPED_TU objects=2 total=2 blocked=0`.
  The original Kbuild is relinked after replacing precisely those two
  applet objects and rebuilding dependent composite objects; all other
  objects remain GCC-produced. **Actual mixed BusyBox `cat` and `echo`
  runtime outputs are byte-identical to the independent GCC baseline:**
  `BUSYBOX_X86_64_MIXED=PASS applets=cat,echo objects=MiniC x86 remainder=GCC GNU_Kbuild=1 runtime=byte-exact`.
  This does **not** claim a complete MiniC-built BusyBox.
- Original TinyCC 0.9.27 still passes **10/10 MiniC-compiled C units**,
  GNU link, executable version oracle and compilation of a separate new
  C application that executes and returns 42. Its next actual feature
  blocker is exposed by unmodified upstream `tests/tests2/00_assignment.c`:
  the GCC-built TinyCC finds
  `/usr/include/x86_64-linux-gnu/bits/wordsize.h` etc., while the
  MiniC-built TinyCC fails resolving `bits/libc-header-start.h`
  from `/usr/include/stdio.h`, even when invoked with the same
  upstream-prescribed `-B<build> -I<upstream>/include -I<upstream>`
  options. An optional `-vv` differential diagnostic now emits
  both compiler search traces. **Do not hide this with a forced
  system `-I` and claim an upstream test pass**. Next investigate
  TinyCC `tcc_split_path` / `tcc_add_sysinclude_path` runtime behavior
  and the backend's generated default include path representation.
- Original SQLite 3.54.1 GCC byte-exact SQL runtime remains green in
  the same focused CI. A complete new RV64 Linux 2064/QEMU P1 and
  a complete MiniC-built BusyBox/TinyCC upstream suite were not run.

The temporary dedicated branch execution modifications to
`.github/workflows/toolchain-focused-regressions-v1.yml` are reverted
to the **original main blob** `cd9a880a5a707f28b5ea63c364c1844ab35e1c56`
after collecting actual CI evidence. Existing unrelated
`linux-runtime-gcc-baseline.yml` failures remain separate.

## TinyCC 24-program upstream suite + BusyBox four-applet native runtime — 2026-10-19

[Focused CI #39175525331](https://github.com/yituanxing/minic-toolchain/actions/runs/39175525331)
completed **success** with unchanged SQLite 3.54.1 byte-exact GCC
SQL oracle and eight independent RV64/MiniAR/MiniLD regression owners
(`TOOLCHAIN_FOCUSED_T1=PASS`).

**TinyCC 0.9.27, frozen pristine original source:** the original
10 TinyCC TUs remain MiniC x86-64 compiled, GNU assembled/linked,
and the resulting native `tcc` executes and compiles new C programs.
The next stage now passes **24/24 hand-selected, original unmodified
`tests/tests2/*.c` C programs**, with actual relocatable objects
compiled separately by MiniC-built TinyCC and GCC-built TinyCC, linked
using the same GNU host compiler and `-lm`, and execution exit codes,
stdout and stderr byte-for-byte equal. The suite includes records, arrays,
pointers, recursion, floating point, quicksort, Hanoi, integer literals,
type coercion, enum and typedef parsing. Exact CI marker:
`TINYCC_X86_64_TESTS2_SUITE=PASS cases=24 original_upstream=1`.
This is a **selected subset**, not the full upstream TinyCC test suite.

**Root cause of the prior TinyCC header lookup discrepancy:** the
initial MiniC driver probe preprocessed the 10 original TUs with
`-DTCC_TARGET_X86_64 -DONE_SOURCE=0`, but omitted the native
`CONFIG_TRIPLET` definition supplied by upstream
`./configure` through `config.mak`'s `NATIVE_DEFINES+=-DCONFIG_TRIPLET=`
line. Without that compile-time setting, default `tcc.h` system
include path construction omits
`/usr/include/x86_64-linux-gnu` and cannot resolve
`bits/libc-header-start.h`. The fixed probe extracts
`x86_64-linux-gnu` from the **original generated config.mak**,
passes it as the genuine preprocessor macro to all MiniC-built
TinyCC TUs, and does **not** patch upstream C or force an artificial
runtime `-I/usr/include/... ` fallback. The discrepancy was a
build configuration gap, **not a confirmed MiniC codegen bug**.
The GNU `sin` linker failure in `22_floating_point` was likewise
the independent baseline missing its required `-lm`; both oracles
now use the same `-lm`.

**BusyBox 1.36.1, frozen original Makefile and source:** scoped
original `cat.c`, `echo.c`, `head.c`, and `wc.c` are all
MiniC x86-64 compiled into GNU-assembled objects and substituted into
the source's genuine GCC-produced Kbuild graph. The four associated
applets are all executed after rebuilding and GNU relinking, with
byte-identical output against the GNU-only BusyBox. Specific CI marker:
`BUSYBOX_X86_64_FRONTIER=SCOPED_TU objects=4 total=4 blocked=0`
and `BUSYBOX_X86_64_MIXED=PASS applets=cat,echo,head,wc ...`.
**Not** a full MiniC-built BusyBox. Other real applets (`grep`,
`cut`, `sort`, etc.) are candidates for a separate incremental gate,
but they were not yet tested by the completed focused CI.

No existing compiler correctness implementation was changed during
this substage; reusable test oracle and exact original upstream build
flags were improved. Preserve 24-program TinyCC and four-applet
BusyBox gates as the current qualified baseline; extend coverage by
small independently diagnosable groups and fix only demonstrated C
semantics or ABI discrepancies. Full latest RV64 Linux 2064/QEMU P1
was not rerun. The branch-execution temporary
`.github/workflows/toolchain-focused-regressions-v1.yml` is restored
to the main blob `cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.
The unrelated inherited failing GCC baseline workflow is not counted
as passing CI.

## New original TinyCC 36-program and BusyBox 9-applet checkpoint — 2026-10-19

The [focused regression run #39178856257](https://github.com/yituanxing/minic-toolchain/actions/runs/39178856257)
completed with **success**, including SQLite 3.54.1 GCC byte-exact database
runtime and `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8`.

- **TinyCC** release 0.9.27 remains pristine: all ten C translation units
  compile with MiniC x86-64 and GNU assemble/link into a real executable,
  which then compiles a new C consumer whose linked executable returns 42.
  The expanded test scans unmodified upstream `tests/tests2` programs:
  **`TINYCC_X86_64_TESTS2_SUITE=PASS cases=36 original_upstream=1`**.
  All 36 independent C test programs were compiled by the MiniC-built
  TinyCC into object files and run with GNU linking. The independent
  GCC-built TinyCC returned byte-identical stdout/stderr and exit
  statuses for each selected test. This is **not** TinyCC's entire
  upstream suite nor self-hosting. The prior multiarch-header difference
  was an omitted original configure macro, not an established backend
  bug; the probe now uses original generated build flags.
- **BusyBox** release 1.36.1 remains pristine. Original
  `cat.c`, `echo.c`, `head.c`, `wc.c`, `grep.c`,
  `cut.c`, `sort.c`, `uniq.c`, and `tr.c` compile with
  MiniC x86-64 and GNU assemble into genuine objects:
  **`BUSYBOX_X86_64_FRONTIER=SCOPED_TU objects=9 total=9 blocked=0`**.
  These exact nine objects replace their GCC equivalents, while all
  other code stays GCC-built; genuine BusyBox Kbuild relinks the
  program. Its nine applets run with byte-exact output against the
  all-GCC reference:
  **`BUSYBOX_X86_64_MIXED=PASS applets=cat,echo,head,wc,grep,cut,sort,uniq,tr`**.
  This is **not** all-BusyBox MiniC compilation.
- The remaining BusyBox GNU attributed record, C99 adjusted-array
  parameter and namescope frontiers were addressed in general front-end
  handlers in commits preceding this checkpoint; tests and the nine
  original applets qualify only those exercised paths. Do not claim
  general GCC/LLVM-equivalent C support or the latest full RV64 Linux
  2064/QEMU P1 at this HEAD.

Temporary branch-specific focused workflow edits are **restored
byte-for-byte to main** at blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.
The separate legacy `linux-runtime-gcc-baseline.yml` job still
fails and is not included in the passing scoped CI claims.

## Original BusyBox 12 MiniC object mixed-Kbuild checkpoint — 2026-10-19

**Actual [focused CI #39180862693](https://github.com/yituanxing/minic-toolchain/actions/runs/39180862693) — SUCCESS**, code/test runner commit `76d1e6b19094b76991872d6fc1046e607421cc2b`.
This check reuses the existing focused job with a temporary branch-scoped opt-in step, not a new workflow. `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1` confirms independently owned RV64, MiniAR and MiniLD checks stayed green.

The existing certified **nine** applets still compile to real MiniC-produced x86-64 objects and pass a byte-exact GNU-only vs original-Kbuild mixed execution oracle. A second, separately reported opt-in cohort adds the **unchanged, Git-object-SHA-verified BusyBox 1.36.1** `coreutils/basename.c`, `dirname.c` and `printf.c`. Original `allnoconfig` + `oldconfig` explicitly enable those three *without editing upstream application C*; an all-GCC Kbuild binary executes first to freeze expected stdout. GNU preprocessing, native MiniC codegen, GNU assembly and real BusyBox Kbuild object replacement were then executed.

Exact CI markers:
- `BUSYBOX_X86_64_EXTRA_GCC_ORACLE=PASS applets=basename,dirname,printf`.
- `BUSYBOX_X86_64_EXTRA_TU=PASS` for all three original C files (preprocessed 4,751 / 4,723 / 5,017 lines respectively).
- `BUSYBOX_X86_64_EXTRA_FRONTIER=DISCOVERY compiled=3 total=3 blocked=0`.
- `BUSYBOX_X86_64_EXTRA_MIXED=PASS applets=basename,dirname,printf extra_objects=3 total_MiniC_objects=12 GCC_reference=byte-exact GNU_Kbuild=1`.

**Correct scope:** 12 MiniC-built applet objects, all other BusyBox objects still GCC-built. The three new applets are checked with one deterministic fixture each; their complete option matrix, the entire BusyBox build under MiniC, and an x86-native Mini preprocessor/assembler/linker are **not** certified. The extra cohort remains opt-in via `BUSYBOX_X86_64_EXTRA_DISCOVERY=1`; default earlier nine-applet qualification is unchanged. A generic CI green result alone must not be used as proof the opt-in cohort passed: verify the explicit `EXTRA_MIXED=PASS` marker. No compiler source implementation changed to obtain this result.

**Next:** promote this newly proven 12-object combination to a durable regression check with an explicit hard-success marker before selecting further original applets, or use a new real workload that exercises unqualified SysV ABI features. Preserve the project-driven decision not to extract Machine IR or rewrite RV64 without evidence.

### BusyBox-12 permanent fail-closed test target verified

Commit `7df191962d7fb40a6fd748a05946fe8621cdd6b3` installs a reusable
`make check-x86_64-busybox-12` target, reusing the existing
`tests/external/busybox/probe-x86_64.sh` without introducing a second
BusyBox driver or a new workflow. The target captures the underlying probe
output and **requires the exact** `BUSYBOX_X86_64_EXTRA_MIXED=PASS ... total_MiniC_objects=12`
success marker. A discovery-mode early exit cannot masquerade as a green
12-object acceptance.

The first attempted Make wrapper [#39181166375](https://github.com/yituanxing/minic-toolchain/actions/runs/39181166375)
was **red for a test-harness environment defect**, not a MiniC codegen
failure: the outer `make CFLAGS=-Werror` leaked its command-line override
via `MAKEFLAGS` into pristine BusyBox Kbuild, turning an upstream
`libbb/dump.c` `-Wformat-security` warning into an error. The wrapper
now explicitly clears inherited make flags and MiniC-only `CFLAGS` before
running the original upstream Kbuild. It does **not** patch any BusyBox
source or turn off MiniC's own `-Werror` build.

**Final [focused run #39181471987](https://github.com/yituanxing/minic-toolchain/actions/runs/39181471987) — SUCCESS**:
the permanent Make target prints and strictly checks
`BUSYBOX_X86_64_EXTRA_FRONTIER=DISCOVERY compiled=3 total=3 blocked=0` and
`BUSYBOX_X86_64_EXTRA_MIXED=PASS ... total_MiniC_objects=12 GCC_reference=byte-exact GNU_Kbuild=1`.
The same run completed `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.
The temporary branch-local focused-workflow edits were **restored
byte-for-byte** to main blob `cd9a880a5a707f28b5ea63c364c1844ab35e1c56`
after the run. Pre-existing broken `linux-runtime-gcc-baseline.yml`
dispatch is still independent.

Current reproducible native gate:

```sh
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/busybox-x86 \
  build/busybox-x86/bin/minic-cc
make MODE=release CFLAGS=-Werror BUILD_DIR=build/busybox-x86 \
  check-x86_64-busybox-12
```

This is a genuine twelve-object BusyBox **mixed** GCC/MiniC Kbuild/runtime
qualification; it is not the full BusyBox C corpus, and does not certify
a post-TinyCC fresh 2064-object Linux/QEMU regression. The normal nine-applet
discovery baseline remains available; the new Make target is the strong
12-object gate for subsequent changes.

## BusyBox all-applet Kconfig pressure — first 594 real C owner sweep (2026-10-19)

The user authorized enabling the entire original BusyBox program set, not
only individually cherry-picked applets. The independent opt-in diagnostic
`make check-x86_64-busybox-allyes-discovery` therefore runs pinned,
unmodified BusyBox 1.36.1 with upstream **`make allyesconfig`** and
enumerates **actual C compiler invocations from real Kbuild**. A transparent
CC wrapper always calls the real GNU compiler with its original arguments,
then separately calls GNU preprocessing, MiniC x86-64, and GNU assembly.
No MiniC object is substituted into BusyBox in this discovery mode;
the existing certified 12-object mixed Kbuild test is unchanged.

**Exact all-config qualification boundary:** upstream `allyesconfig` enables
964 Kconfig symbols on the Ubuntu x86 host. Only the **`CONFIG_WERROR` warning
policy** is then disabled and passed through upstream `oldconfig` so newer
libselinux's deprecated `security_context_t` declarations do not abort the
original unmodified C build. The effective config has **963 symbols set to
`y`**. This is not identical to an untouched 964-symbol allyesconfig.
All software applets remain requested by the Kconfig generator subject to
Kconfig dependencies; the 963 count is all kinds of options, **not** 963
individual applets or 963 C files. Additional host optional headers
(libselinux, libsepol, libtirpc, libcap, libcrypt) were installed only in
the temporary diagnostic CI environment, not vendored into MiniC.

**First broad discovery**:
- [#39182591019](https://github.com/yituanxing/minic-toolchain/actions/runs/39182591019)
  checked 594 actual BusyBox Kbuild C translation units but exposed a
  common glibc/libselinux typedef parsing deficit:
  only **6/594** generated MiniC x86 native objects, while 584 MiniC
  failures shared the *same* `unsupported GNU typedef attribute` error.
- The existing `src/frontend/attribute.c` already recognizes
  `deprecated`/`__deprecated__` as a diagnostic-only GNU attribute valid
  on types. The typedef consumer accidentally accepted `mode`,
  `may_alias`, `aligned`, `transparent_union`, but unconditionally
  rejected `deprecated`. In
  `src/frontend/parser_typedef.c::consume_typedef_attribute`,
  accepting **only `MINIC_ATTRIBUTE_DEPRECATED`** on the already-verified
  TYPE target preserves original C type/ABI meaning and does not discard
  layout-bearing attributes or rewrite upstream headers.
- A new reusable fixture in
  `tests/compiler/c0/run-x86_64-native-m1.sh` confirms GCC runtime 42,
  MiniC x86 native runtime 42 and RV64 assembly emission for
  the original `deprecated` and `__deprecated__` typedef spellings.

**Final scoped validation**
[focused #39182849843](https://github.com/yituanxing/minic-toolchain/actions/runs/39182849843)
**SUCCESS**, including the new native GNU typedef C regression and
`TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.
On the same 963-enabled-symbol original BusyBox configuration and real
Kbuild invocation list:
- `BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=963 attempted_C_TUs=594 mini_objects=542 cpp_fail=4 minic_fail=48 asm_fail=0 gcc_fail=5 gcc_full_build=FAIL full_MiniC_busybox=NOT_TESTED`.
- **542/594** produced independent MiniC x86-64 assembly and GNU
  relocatable objects. This is approximately 91% of **attempted C
  translation units**, and NOT 91% of BusyBox programs fully built,
  linked, or run.
- The **48 MiniC** blockers comprise roughly 28 previously unsupported
  x86 Core CALL/ABI cases (kind 47), seven attribute-position cases,
  two aggregate global-initializer gaps, one inline-asm constraint
  case, and smaller semantic/other exceptions. Distinct host failures
  include four missing/unsupported C header preprocess paths
  (PAM `security/pam_appl.h`, legacy `rpc/rpc.h`) and five GNU
  original object-build failures; counts overlap the same TU owners
  and should not be added to the 48.
- The **original whole-project GCC build is still RED** in this maximal
  configuration due to upstream/environment feature incompatibilities.
  Therefore there is no fully linked all-enabled BusyBox or whole-suite
  runtime comparison; any claim that all 594 or all applets passed is
  incorrect.

The reusable full-config frontier is diagnostic-only and **does not
silently turn failure into a runtime pass**. Per-TU source/object,
GCC/CPP/MiniC/assembler outcomes and stderr are retained in immutable
[CI artifact `busybox-allyes-x86-frontier-v1` (artifact 12156902895)](https://github.com/yituanxing/minic-toolchain/actions/runs/39182849843).
Temporary changes to the pre-existing focused workflow were restored
byte-for-byte to main's original blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56` after the result.
The unrelated legacy malformed Linux workflow remains a separate issue.

**Next concrete real-project seam:** inspect the 28 `Core kind 47` call
failures with the x86 CALL ABI trace on their exact preprocessed input;
categorize by source-verified record/class/register/stack cases before
changing any ABI code. Address missing GCC baseline dependencies
separately. Keep the 12-object linked/run oracle as the true end-to-end
regression; no premature generic Machine IR or untested RV64 changes.

## BusyBox maximal C-owner follow-up: 542 → 573 → 576 (2026-10-19)

This is **real original-Kbuild MiniC x86-64 object coverage**, **not** full
BusyBox runtime or 576 independently tested applets. The unchanged pinned
BusyBox 1.36.1 source tree uses original `allyesconfig`, with only host
warning-policy `CONFIG_WERROR` disabled (963 enabled Kconfig symbols).
Every GNU Kbuild CC invocation still builds the original GNU object, while
a shadow MiniC invocation preprocesses the same original C source and
independently emits one x86-64 GNU-assembled object. No shadow object is
substituted in this discovery workload. The 12-app mixed-GNU/MiniC linked
runtime test remains the separate actual executable acceptance gate.

### Problem found from **actual** 29 x86 CALL trace diagnostics

A fresh diagnostic [#39183565630](https://github.com/yituanxing/minic-toolchain/actions/runs/39183565630)
proved that all 29 observed `X86_CALL_GATE` failures were
`stage=arguments`, not record-return or an unsupported Core opcode
implementation. Their original callee signatures included:
- `getsockname`, `sendto`, `bind`, `accept`, `connect`,
  `recvfrom`: GNU libc's transparent sockaddr pointer union passed
  **by value**, SysV INTEGER one-eightbyte class.
- `inet_ntoa`: four-byte `struct in_addr` passed by value, also
  one INTEGER eightbyte.
- Variadic `semctl`: object-form variadic union passed in the next
  integer register / argument stack slot.
- `service(struct stat, ...)` in `miscutils/devfsd.c` is a **distinct
  larger aggregate** and remains deliberately unsupported.

Prior x86 code recognized only SQLite Token's specially verified
16-byte two-GPR record. The added **backend-owned**
`x86_one_gp_record` classifier validates complete un-packed 1/2/4/8
byte integer/pointer-only records with natural member offsets and rejects
floats, nested records, bitfields, non-natural/misaligned/packed cases.
Incoming and outgoing fixed/variadic object-argument handling now uses
the appropriate SysV GP register or stack location. Exact-width memory
loads preserve object bounds, and the existing two-GPR case is retained.

The new `make check-x86_64-one-gp-sysv` functional oracle exercises
MiniC→GCC and GCC→MiniC real calls with `struct in_addr`-shaped 4B
record, transparent pointer union, variadic union and a spilled seventh
integer-register-class argument. The
[focused run #39184175473](https://github.com/yituanxing/minic-toolchain/actions/runs/39184175473)
passed:
`X86_ONE_GP_SYSV=PASS gcc_to_minic=1 minic_to_gcc=1 transparent_union=1 fixed_record=1 variadic_union=1 spilled_record=1`,
plus the existing x86 M1 and RV64/MiniAR/MiniLD focused checks.

The whole-config + existing 12 applet
[run #39184302804](https://github.com/yituanxing/minic-toolchain/actions/runs/39184302804)
then passed its scoped CI and found:

| Metric | Before | After one-GP x86 ABI |
| --- | ---: | ---: |
| Real Kbuild C TU attempts | 594 | 594 |
| MiniC x86 assembly + GNU object succeeded | 542 | **573** |
| MiniC blockers | 48 | **19** |
| Host GNU preprocessing blockers | 4 | **2** |
| Complete GNU BusyBox allconfig image | blocked | blocked |
| Certified mixed executable applets | 12 | **12** (preserved) |

The increase is **31** native objects for no new shared Core machine-level
representation or changed RISC-V ABI.

### The next original Kbuild pressure: local `may_alias`

Inspection of the actual archived preprocessed source shows three
separate failures from local pointers with no-argument
`__attribute__((__may_alias__))`, in `libbb/lineedit.c`,
`libbb/signals.c` and `util-linux/mdev.c`. The shared attribute table
formerly allowed `may_alias` on TYPE targets but incorrectly rejected
this GNU local pointer declarator position as invalid on OBJECT. Since
MiniC currently has **no type-based alias analysis (TBAA)**, accepting
precisely this no-argument local optimization marker is conservative;
no optimizer is permitted to add TBAA later without preserving its
semantic alias-relaxing effect. This change does not swallow unknown
attributes, alter type layout, change machine ABI or downgrade alignment.

Added GCC native runtime, MiniC native runtime and RISC-V assembly
fixtures for the original local GNU attribute spelling.

[Final focused #39184810279](https://github.com/yituanxing/minic-toolchain/actions/runs/39184810279)
**SUCCESS**:
- `X86_ONE_GP_SYSV=PASS` and
  `PASS: GNU no-argument may_alias local pointer x86/RV64/GCC oracle`.
- `BUSYBOX_X86_64_EXTRA_MIXED=PASS ... total_MiniC_objects=12 GCC_reference=byte-exact GNU_Kbuild=1`.
- `BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=963 attempted_C_TUs=594 mini_objects=576 cpp_fail=2 minic_fail=16 asm_fail=0 gcc_fail=3 gcc_full_build=FAIL full_MiniC_busybox=NOT_TESTED`.
- `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.

**Final observed delta: 542 → 576 successful native objects (+34);
MiniC failures 48 → 16.** This is **576 / 594 = 96.97%** *of attempted
C translation units*, not executable coverage. GCC's full
allyesconfig build still fails on removed/changed kernel TC CBQ API
declarations (e.g. `TCA_CBQ_MAX`) and on unresolved historical RPC
header paths (`rpc/rpc.h`); do not claim successful full GCC or MiniC
link, boot, runtime or completed x86 MiniAS/MiniLD.

The remaining 16 MiniC blockers include:
- Three source `__attribute__((constructor))` uses inside BusyBox's
  original test registration; these have **link/runtime initialization
  semantics** and MUST NOT be silently ignored merely to get a green
  compilation counter.
- An aggregate `struct stat` memory-class by-value call; **not**
  a one-GP extension.
- Larger global initializer/relocation cases, inline asm register
  constraints, GNU parameter syntax, and several source semantic/Core
  cases. Each must be handled against the original fixture and GCC
  behavior, not with a blanket attribute-ignore or unsafe ABI fallback.

Reusable suite entry points:
`make check-x86_64-one-gp-sysv` for real bidirectional ABI,
`make check-x86_64-busybox-12` for mixed Kbuild runtime,
`make check-x86_64-busybox-allyes-discovery` for honest per-TU
maximal-config coverage with retained original CPP/MiniC/GNU stderr.
The CI artifact is
[`busybox-allyes-one-gp-candidate-v1`](https://github.com/yituanxing/minic-toolchain/actions/runs/39184302804);
a second full-config artifact is attached to
[#39184810279](https://github.com/yituanxing/minic-toolchain/actions/runs/39184810279).

All temporary modifications of the existing focused workflow were
restored **byte-for-byte** to main blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56` after the final run.
No new CI workflows or broad architecture rewrite. A fresh full 2064/2064
RV64 Linux+QEMU P1 gate is still required before claiming latest shared
frontend changes are Linux-certified.

## Original BusyBox GNU constructors: 576 → 579 owner objects (2026-10-19)

A new real-source blocker group came from three unchanged original
BusyBox `CONFIG_UNITTEST` registration definitions in
`libbb/compare_string_array.c`, `libbb/obscure.c`, and
`libbb/strrstr.c`. Every one used the exact spelling
`static void __attribute__((constructor)) register(void) { ... }`.
GCC calls those functions automatically before `main`; treating this
attribute as ignored metadata would compile code with incorrect startup
semantics and would not qualify the files.

Instead, the shared GNU attribute descriptor now explicitly identifies
`constructor`/`__constructor__` as a **language-semantic,
non-parse-only attribute**. Function parsing persists a constructor bit
on the **actual defined void(void) function**, and marks its body
reachable even when static and not explicitly called in C source.
Unsupported priority arguments and invalid function signatures remain
fail-closed (this is not generic constructor-priority support).
The two independent ELF writers own the correct machine-specific
startup table: x86-64 emits an ELF `.init_array` function-pointer
relocation via `.quad`, and RV64 via `.dword`. No Core ISA instruction,
target ABI sharing, or unsafe extra constructor C call was introduced.

The reproducible `make check-elf-constructors` oracle compares GNU and
MiniC binaries that have **two static, otherwise-unreferenced constructor
functions**. Each constructor updates a shared counter, and `main`
checks the final state. Assertions cover actual linked-and-run startup
effects (not just successful assembly), correct `.init_array`
relocations, x86 host execution, RV64 static link + QEMU execution,
and rejection of priority/signature cases. The first draft check was
red because it assumed readelf relocations would name local functions;
GNU correctly emits a relocation to a section symbol. The oracle was
corrected to verify the `.rela.init_array` section, with independent
runtime execution proving both constructors' semantics.

Confirmed [focused #39186118041](https://github.com/yituanxing/minic-toolchain/actions/runs/39186118041) **SUCCESS**:
- `X86_CONSTRUCTOR_ELF=PASS init_array_relocations=2 before_main=1 gcc_oracle=1`.
- `RV64_CONSTRUCTOR_ELF=PASS init_array_relocations=2 qemu_before_main=1 gcc_oracle=1`.
- `CONSTRUCTOR_NEGATIVE=PASS unsupported-priority=fail-closed bad-signature=fail-closed`.
- Existing x86 M1 + focused RV64/MiniAR/MiniLD check remained green.

A separate unchanged BusyBox real-Kbuild 12-object mixed runtime
and original GNU-based all-config shadow compiler scan passed
[focused #39186213088](https://github.com/yituanxing/minic-toolchain/actions/runs/39186213088):
- `BUSYBOX_X86_64_EXTRA_MIXED=PASS ... total_MiniC_objects=12 GCC_reference=byte-exact GNU_Kbuild=1`.
- `BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=963 attempted_C_TUs=594 mini_objects=579 cpp_fail=2 minic_fail=13 asm_fail=0 gcc_fail=3 gcc_full_build=FAIL full_MiniC_busybox=NOT_TESTED`.
- `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.

Thus the unchanged 594 original C owner workload advanced **576/594
→ 579/594**, with MiniC codegen failures falling **16 → 13**.
This is **97.47% of attempted C translation units producing independent
GNU-assembled MiniC x86-64 objects**, **not** 579 runnable BusyBox
applets, nor a successfully linked whole BusyBox program. The 12-applet
mixed runtime gate remains separately verified; all-enabled full GCC
Kbuild still encounters historical host RPC header paths and removed
kernel TC CBQ interfaces.

The temporary re-use of existing focused CI workflow was restored
byte-for-byte to the original main workflow blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`, with no added CI
workflows. The two-backend architecture decision stands: keep
startup behavior as shared C declaration semantics but delegate
ELF serialization to each target backend.

Next source-driven steps: inspect and test the 13 remaining MiniC
failures (e.g. larger `struct stat` memory-class ABI, global relocations,
GNU parameter attributes and constant-expression edges), and in
parallel qualify a separate host-compatible original GNU BusyBox
configuration so that additional original applets can be truly linked
and executed. Fresh 2064-object RV64 Linux + QEMU P1 on the latest
shared frontend source remains a merge gate and is not implied by
the focused RV64/QEMU constructor runtime check.

### Next blocker classification: do not misclassify string subscripts as ICE

`libbb/vfork_daemon_rexec.c` still fails on the original
`sizeof(char[1 - 2*!!("/proc/self/exe"[0] != '/')])` expression.
A tempting change was to make the constant evaluator globally fold
`"string"[0]` as an integer constant expression. A direct independent
GCC `-std=gnu11 -Werror` check refuted that broad rule:
`_Static_assert("A\\x42"[1] == 'B', ...)` is **not** an allowed C ICE.
The expression is accepted in BusyBox's local `sizeof` context because
a function-scope variably-modified array bound can be runtime-evaluated,
not because the subscript is a valid ISO C ICE. The speculative
`const_eval.c` change was therefore reverted **byte-for-byte**
before any acceptance claim. The right fix is to inspect and preserve
the function-scope VLA/sizeof evaluation path and GCC behavior.
The verified **579/594** checkpoint is unchanged.

## GNU original BusyBox nearly all-enabled full native Kbuild baseline — 2026-10-19

The previous all-enabled 963-symbol Kconfig scan was **not linkable by GCC**
on the current GitHub Ubuntu host because upstream BusyBox 1.36.1's
historic SunRPC headers, old Linux TC CBQ UAPI, and mutually incompatible
`CONFIG_STATIC=y` plus `CONFIG_DEBUG_SANITIZE=y` host flags. That was a
baseline/host configuration issue, not a MiniC executable regression.
To establish a real whole-BusyBox oracle **without modifying any original C**,
the existing `allyes_sweep.py` now has an explicit opt-in
`BUSYBOX_ALLYES_LINK_COMPATIBLE=1` mode that asks upstream
`allyesconfig` then changes only six Kconfig choices before upstream
`oldconfig`: `WERROR`, `TC`, `FEATURE_INETD_RPC`,
`FEATURE_MOUNT_NFS`, `STATIC`, `DEBUG_SANITIZE`.
This gives **957 enabled symbols**. It is intentionally **not** the
unchanged full `allyesconfig` configuration, and 957 means *Kconfig
symbols*, not 957 independent applets or 957 source files.

The existing original GNU compiler/real Kbuild builds the **whole original
BusyBox executable** under this host-compatible configuration, using
the existing transparent compiler proxy. MiniC still runs on independent
preprocessed snapshots and creates shadow objects; **none of those shadow
objects are linked in this full-program baseline**.

**Actual successful [focused CI #39187794876](https://github.com/yituanxing/minic-toolchain/actions/runs/39187794876):**

- `BUSYBOX_ALLYES_GCC_KBUILD=PASS original_Kconfig=1 original_sources=1`.
- `BUSYBOX_ALLYES_GCC_RUNTIME_ORACLE=PASS applets=6 original_full_Kbuild=1`.
- Native original GNU BusyBox `sed`, `awk`, `grep`, `sort`,
  `sh`, `find` each ran its deterministic real execution fixture
  successfully. The first probe erroneously requested `ash`, an
  applet name *not selected* by this all-enabled Kconfig variant; the
  exact GCC stderr was `ash: applet not found` (127), and the oracle was
  corrected to test the real `sh` dispatcher rather than disabling
  or inventing software.
- `BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=957 attempted_C_TUs=594 mini_objects=582 cpp_fail=0 minic_fail=12 asm_fail=0 gcc_fail=0 gcc_full_build=PASS full_MiniC_busybox=NOT_TESTED`.
- `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.

**Interpretation:**

| Measured outcome | Exact scope |
| --- | --- |
| 594 real original Kbuild C owner attempts | Host-compatible 957-symbol config |
| 582 independently assembled MiniC x86 objects | Shadow objects; not actually linked or executed in this mode |
| 12 MiniC codegen/frontend failures | Separate pending compiler blockers |
| 0 GNU preprocessing/assembly failures | Original C source compilation input |
| Original **complete BusyBox GNU link** | **PASS** |
| Six native full-GNU applet runtime cases | **PASS** |
| 12 MiniC/GCC mixed applet objects | Previous independently certified existing gate, unchanged |
| Mixed 582-object BusyBox whole executable | **NOT TESTED**, and not certified |

This is a major change in *test readiness*: there is now an independently
running original GNU full-BusyBox executable, so the next step is to promote
real staged MiniC object substitution with byte-exact **whole-executable
differential** rather than endlessly optimizing detached compile counters.
Start with `sed`, `awk`, `find`, `grep`, `sort` original Kbuild
objects under the **same frozen 957-symbol** configuration, preserving the
original GNU-built executable as oracle. Only accept each or combined
cohort after full Kbuild re-link, deterministic actual applet runtime and
cross-target focused regressions. No commit in this checkpoint claims those
five were already replaced.

The reusable fail-closed native full-GNU link target is
`make check-x86_64-busybox-host-link`, and the same test records each
shadow original file's detailed stderr plus its input and output objects.
The temporary branch-local focused CI workflow changes were restored
byte-for-byte to the baseline blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56` after collecting CI
evidence. No new workflow was permanently added. The existing
broken legacy Linux baseline YAML continues to fail dispatch independently.

A fully fresh RV64 2064-source Linux+QEMU P1 qualification on *current*
feature-head code still has not been run; prior focused RV64 successes do
not substitute for that merge gate.

## BusyBox real mixed full-executable x86-64 re-link: five complex applets — 2026-10-19

**This is a new executable-level milestone, beyond earlier independent
582/594 `.o` shadow compilation.** From the pinned BusyBox 1.36.1
unmodified C and its **957 enabled-option original upstream Kconfig/Kbuild**
(the previous host-compatible full-GNU baseline), the new opt-in
`make check-x86_64-busybox-complex-five` builds and freezes a genuinely
working all-GNU BusyBox executable as an independent oracle.

The exact five unchanged original C owners are:

| Original BusyBox Kbuild C owner | MiniC x86-64 object replaced? | Independent full mixed BusyBox runtime |
| --- | --- | --- |
| `editors/sed.c` → `editors/sed.o` | **Yes** | **PASS** |
| `editors/awk.c` → `editors/awk.o` | **Yes** | **PASS** |
| `findutils/find.c` → `findutils/find.o` | **Yes** | **PASS** |
| `findutils/grep.c` → `findutils/grep.o` | **Yes** | **PASS** |
| `coreutils/sort.c` → `coreutils/sort.o` | **Yes** | **PASS** |

Each owner was verified by GCC preprocessing the actual Kbuild command,
MiniC x86-64 assembly generation, independent GNU assembly, and **real
GNU Kbuild re-linking of the entire BusyBox executable** with only that
one owner's object substituted, followed by **11 byte-for-byte native
fixture comparisons** (combined stdout, stderr, exit code) with the
frozen GCC binary. The object was then actually restored and the complete
GNU runtime differential re-run before testing the next owner.

**Critical testing loopholes found and closed before acceptance:**

1. BusyBox selects its applet by **`argv[0]`**. A copied GCC reference
   executable named `gcc-reference-busybox` gave 127/app-not-found
   while the mixed binary correctly ran real `sed`, `awk`, etc.
   This cannot be treated as a compiler error. The independent
   reference was copied to `report/gcc-reference/busybox`, explicitly
   checked against known GNU runtime outputs, and only then used
   as the differential oracle.
2. Merely copying MiniC `.o` into a precompiled BusyBox tree before
   asking Kbuild to rebuild its parent archive was **not reliable**:
   the original Kbuild `.cmd`/dependency logic recompiled the
   applet through GCC and overwrote the MiniC object. An early run
   [#39189278828](https://github.com/yituanxing/minic-toolchain/actions/runs/39189278828)
   correctly failed this false-ownership case. The corrected
   substitution is now attached to the **exact original Kbuild CC
   invocation for the desired output owner**. The proxy invokes GNU CC
   with the unchanged original command, then replaces that specific
   fresh target object with the previously audited MiniC object
   *before* Kbuild's aggregate archive/link stage. The test requires
   its explicit owner-replacement receipt and **SHA-256 identity
   with the MiniC object after Kbuild finishes**. Therefore Kbuild
   silently rebuilding the object through GCC cannot pass.
3. GNU BusyBox binary build IDs can change during an otherwise
   semantically equivalent Kbuild re-link. The restored GNU state
   is instead required to have an original GCC object, a successful
   full real Kbuild rebuild and **all eleven** runtime outcomes
   byte-for-byte identical to the frozen original GNU oracle.
   A different binary hash is recorded as a diagnostic, not
   misinterpreted as a program failure.

**Important additional combined acceptance:** After all five independent
one-owner tests passed, the test deliberately removed the corresponding
five Kbuild objects and parent aggregates and routed **all five distinct
MiniC objects simultaneously** into a **sixth full BusyBox executable**.
The GNU compiler, original Kbuild, and linker still build all remaining
C owners. The same eleven real applet/shell fixtures ran against the
frozen original GCC executable with exactly equal stdout, stderr and
exit code. The test individually checked **all five live MiniC object
SHA-256 identities and the five original CC-owner replacement receipts**
inside this *single* resulting executable before allowing a green result.
This catches cross-applet linkage and interaction defects that isolated
successful translation units would miss.

**Actual successful
[CI #39190086179](https://github.com/yituanxing/minic-toolchain/actions/runs/39190086179):**

- `BUSYBOX_MIXED_COMPLEX_GNU_REFERENCE=PASS ... tests=11`.
- Each of `sed`, `awk`, `find`, `grep`, `sort`:
  `BUSYBOX_MIXED_COMPLEX_APPLET=PASS ... MiniC_object=1 original_GNU_Kbuild_link=1 GCC_11_fixture_byte_exact=1 binary_changed=1`.
- `BUSYBOX_MIXED_COMPLEX_FRONTIER=DISCOVERY candidates=5 mini_objects=5 GNU_links=5 runtime_exact=5 fixtures_per_applet=11`.
- `BUSYBOX_MIXED_COMPLEX_COMBINED=PASS MiniC_owners=5 real_GNU_Kbuild_link=1 independent_GNU_oracle=1 eleven_fixture_byte_exact=1 sha_changed=1`.
- `BUSYBOX_MIXED_COMPLEX=PASS all_five_independent=1 combined_five=1 GCC_original_oracle=1 real_Kbuild_link=1`.
- Original allconfig host-compatible scan independently remains
  `BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=957 attempted_C_TUs=594 mini_objects=582 cpp_fail=0 minic_fail=12 asm_fail=0 gcc_fail=0 gcc_full_build=PASS full_MiniC_busybox=NOT_TESTED`.
- Focused `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.

The compiled verdict is **five MiniC-owned, GNU-linked complex BusyBox
applet object files functioning together in one real executable**, and
their eleven carefully scoped execution fixtures are GCC byte-for-byte
compatible; **this is NOT an all-MiniC BusyBox**. The prior separate
12-object mixed applet test is not claimed to be part of this specific
five-owner combined executable (the two cohorts use separate Kconfig
configurations). Do not add the groups and claim seventeen owners have
already been tested together.

The test harness retains the original GNU executable, the exact original
Kbuild per-TU preprocessing inputs, MiniC-generated objects, GNU compiler
outputs, fresh Kbuild logs for individual and combined links, and
per-owner replacement receipts. The heavy regression is deliberately
**opt-in**, reuses the pinned existing BusyBox fixture and Make target,
and introduces no permanent workflow. Temporary focused-workflow edits
were restored byte-for-byte to main baseline workflow SHA
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.

**Next:** Expand **coexisting** object ownership to 12 + 5 in the *same*
host-compatible full-BusyBox Kconfig, then progressively use exact Kbuild
owner routing to replace more of the 582 successfully generated MiniC
objects, with staged linker diagnostics, real runtime tests, and a
careful crash/byte-diff blame strategy. The untouched original
full-configuration 963-enabled-symbol workload and fresh RV64 full
Linux 2064 + QEMU P1 remain separate qualification boundaries.

## BusyBox 957-option full Kbuild: 581 MiniC object owners concurrently — 2026-10-19

**Milestone: a whole real BusyBox executable successfully links and runs
with 581 distinct original C object owners built by MiniC x86-64.** All
other object owners continue to use the unmodified original Kbuild GNU
toolchain. This is not full MiniC BusyBox, MiniAS, or MiniLD replacement.

The independently frozen reference is a fully linked and runnable
all-GCC BusyBox 1.36.1 under the reproducible host-compatible
957-enabled-symbol Kconfig, all original C unmodified. The existing
`allyes_sweep.py` extracts the actual 594 original GNU CC commands,
preprocesses their source inputs, compiles an independent MiniC
x86-64/GNU-AS object for 582 translation units, and records each exact
source/object/driver outcome. **One generated no-`-o` helper compilation**
(`page_size_14734.c`) has no persistent Kbuild object owner and
therefore is *not* eligible for object substitution. That leaves
**593 materialized original Kbuild `.o` owners**, of which **581
MiniC-compiled objects** and **12 explicit GCC fallbacks** can currently
be linked together.

The new `make check-x86_64-busybox-all-eligible` opt-in strong gate
performs original GNU Kbuild from the same pristine pinned source,
then removes the exact 581 eligible GCC object owners, invalidates all
relevant Kbuild parent archives, and forces fresh original GNU CC
invocations. The shared driver recognizes each requested absolute
object path, lets the original GNU compiler run unmodified, then
substitutes only its corresponding proven GNU-assembled MiniC object
**at that precise CC owner output**, before the original archive and
final GNU linker stages. The test checks all **581 path-unique replacement
receipts** and **all 581 SHA-256 object identities** *after* Kbuild
finishes, to ensure no silent fallback or GCC rebuild erased a MiniC
object. It then checks the resulting whole BusyBox executable differs
from the saved GNU binary and executes 11 deterministic applet
fixtures, comparing exit code, stdout, and stderr byte-for-byte with
the original all-GCC program. Original BusyBox source is never patched.
Unlike the prior five-object milestone, all 581 MiniC owners coexist
in **one linked BusyBox executable**.

**CI [#39191471825](https://github.com/yituanxing/minic-toolchain/actions/runs/39191471825) — SUCCESS** on the original
frontend showed:
`BUSYBOX_MIXED_BULK_SOURCE=DISCOVERY original_Kbuild_owners=593 eligible_MiniC=581 GCC_fallback=12`,
`BUSYBOX_MIXED_BULK_REAL_OWNERS=PASS total=581 SHA256_verified=581 original_Kbuild=1`,
and
`BUSYBOX_MIXED_BULK=PASS MiniC_owners=581 GCC_fallback=12 original_full_Kbuild=1 eleven_fixture_byte_exact=1 MiniC_and_GNU_binary_differ=1 full_all_MiniC=NO`.

An independent source-driven correction to array parameter parsing
fixed a C enum bound `unsigned long meminfo[MI_MAX]` from BusyBox
`procps/top.c` that had incorrectly been rejected as an unknown
preceding-parameter VLA bound. The shared frontend now differentiates
a genuine enum ICE from a preceding named C99 VLA bound and retains
the existing fixed-bound evaluator. A GCC vs MiniC x86 real runtime
42 plus RV64 code-generation fixture is committed. **Final
[#39191910150](https://github.com/yituanxing/minic-toolchain/actions/runs/39191910150) — SUCCESS**:
`PASS: C enum ICE array outer parameter adjusts to pointer on x86/RV64/GCC`,
the entire 581-owner replacement and 11 runtime oracle reran successfully,
and the focused RV64/MiniAR/MiniLD regression finished
`TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1`.
The `procps/top.c` owner's next exposed blocker is now its
**x86-64 aggregate ABI CALL lowering** (`display_process_list`, Core
kind 47), so it correctly remains a GCC fallback; the shared frontend
improvement did not invent a compiler coverage win.

**The 12 true still-GCC-owned original objects** are:
`libbb/hash_md5_sha.o`, `libbb/verror_msg.o`,
`libbb/vfork_daemon_rexec.o`, `libbb/xconnect.o`,
`miscutils/devfsd.o`, `miscutils/hdparm.o`,
`networking/ifconfig.o`, `networking/tls_sp_c32.o`,
`procps/top.o`, `selinux/chcon.o`,
`selinux/setfiles.o`, `util-linux/mkswap.o`.
They remain blocked by different explicit compiler issues: Core
lowering, enum/varying expression semantics, function pointer type
compatibility, SysV larger-aggregate ABI, static data/relocations,
GNU inline asm register constraints, pointer assignment qualification
or GNU declarator attributes. **Do not promote these to MiniC by
ignoring semantic diagnostics.**

**What the result proves:** full actual original-Kbuild link of 581
simultaneous MiniC-compiled object owners plus 12 GCC fallback objects;
all 581 replacements are accounted for and 11 real native whole-program
fixtures compare equal with the independent GCC executable.
**What it does NOT prove:** the other BusyBox applets all behave correctly;
the 11 smoke cases do not exercise every function or every enabled
applet, and 12 fallback units were still compiled with GCC. This
result does not certify an all-MiniC build or complete source coverage.
The 963-symbol pure allyesconfig build and a current full RV64
Linux 2064 + QEMU P1 pass also remain separately unqualified.

The temporary existing focused CI workflow changes were restored
byte-for-byte to main workflow blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.
No new CI workflow was left running as part of the branch.
Tests are persistent Make targets and source-controlled reusable scripts,
not one-off generated output.
