# MiniAS exact-HEAD release acceptance: original Linux and real programs (2026-11-02)

This is an execution and evidence ledger for PR #667. The completed
four-architecture Linux results below belong to refactor commit `5381dc63`.
Any later code/test-only HEAD needs its own exact-head verification; an
earlier success is regression evidence, not a substitute for that check.

## One final-commit trigger

Push to `refactor/minias-target-isa-abi-core` with changes under `assembler/**`
and **all four exact tokens in the final commit message**:

```
[minias-linux-four] [minias-linux-gcc-single] [minias-replay-3x7] [minias-gate-v1]
```

| Workflow | Triggered jobs and acceptance boundary | Required evidence |
| --- | --- | --- |
| `.github/workflows/minias-linux-fourarch-runtime.yml` | `gcc-minias-kbuild-qemu` x86-64/AArch64/LoongArch64; GNU AArch64/Loong controls; `matched-config-provenance` | Original Linux 6.6.143 GNU GCC/Kbuild `.s` assembled **only** by MiniAS, linked by GNU BFD, unmodified system-QEMU boot: per arch **P0 2/2 and P1 12/12**; no GNU-as fallback. Independent controls and exact resolved Kconfig/initramfs SHA parity; AArch64 original production `.s` manifest >=4171 and identical across GNU/MiniAS. |
| `.github/workflows/minias-a0-focused-diagnostics-v1.yml` | `linux-single` RV64 selected by `[minias-linux-gcc-single]` | Original RV64 GNU GCC/Kbuild `.s`, MiniAS-only assembly, GNU BFD link, system-QEMU P0 and **P1 12/12**, 2099+ original production object owners, bypass=0; `MINIAS_LINUX_GCC_RUNTIME=PASS p0=PASS p1=12/12`. This is the fourth Linux architecture, **not** in the three-target workflow. |
| `.github/workflows/minias-linux-pinned-3x7-replay.yml` | `shard` 3 targets x seven frozen shards, then `aggregate` | All 21 frozen-original GNU Linux `.s` shard results; source, Kbuild `.cmd`, authentic `.incbin` and SHA pins validated, zero assembler failures. **Assembly coverage only**: no re-run GCC, GNU object equality, linker, or QEMU P1 implied. Frozen historical owner totals: x86 2763, AArch64 4140, LoongArch64 2742. |
| `.github/workflows/minias-batch-3536.yml` | `exact-head-3536` -> reusable `.github/workflows/minias-a0-gate-v1.yml` | All 3352 frozen Linux inputs, 149 ground-truth and 35 native units (3536 total) actually assembled/verified by the requested exact-head acceptance gate. This does **not** establish original 3x7 GNU Linux Kbuild or guest behavior. |
| `.github/workflows/minias-gcc-runtime-ab.yml` | Four real GNU GCC/GNU AS vs MiniAS/GNU BFD/QEMU-user programs; no marker, `assembler/**` path push | Identical GCC `.s` on both assembler lanes; GNU ELF metadata contracts, GNU BFD static link, runtime output and exit match. |
| `.github/workflows/minias-runtime-ladder.yml` | Four-architecture five-stage programs, real cJSON, upstream zlib and Lua; no marker, `assembler/**` path push | Separate assembly, final GNU BFD link, QEMU-user execution and side-by-side stdout/exit. All relevant independent stage results are required; a small staged-program pass cannot hide a failed full upstream-library stage. |

For large workflows, inspect actual `push` runs on the **final SHA**, not
only PR-triggered check summaries. An opt-in job that is `skipped` is not a
pass, and an artifact download error is an infrastructure blocker rather
than a MiniAS semantic failure.

## Verified original replay source availability

The immutable 3x7 workflow downloads **run 40658346252** (source head
`6a725a15e0b7b6c611f73de10e4c95a03f8637f0`, 2026-10-28). On 2026-11-02
the GitHub Actions artifact API reported **46/46 unexpired**, including all
21 archives `minias-linux-kbuild-{x86_64,aarch64,loongarch64}-shard{0..6}`;
their `expires_at` timestamps are on **2026-11-27 08:49–08:58 UTC**. This
proves availability for the replay now, **not** that the final SHA has
successfully consumed or passed them.

The separate 3536 gate downloads older source artifacts from runs
`33186855250` (seven frozen corpus archives), `33237304382` (one original
`.incbin` sidecar archive), and `33248985705` (C149 corpus archive).
The GitHub Actions API likewise reported **all 9/9 required archives
unexpired** on 2026-11-02, expiring between **2026-11-26 15:47 UTC** and
**2026-11-27 10:57 UTC**. Still require actual retrieval and SHA validation
on the final run before calling an assembler regression.

## Local program acceptance, before the final refactor commit

These 2026-11-02 runs used the then-built MiniAS on base HEAD
`5d75f6ade18679760d4afe82b2036142c8dab063`. The machine has host
`gcc/as/ld`, cross GNU assemblers, but **no QEMU**, cross GCC compilers or
Linux 6.6.143 Kbuild tree. `QEMU_USER=/usr/bin/env` consequently executes
the x86 binaries **natively**; never describe its output field `qemu=2` as
actual QEMU coverage. Historical tests here do not certify later edits.

| Local command/stage | Observed outcome | Scope |
| --- | --- | --- |
| `PYTHONPATH=/usr/lib/python3/dist-packages MINIAS=$PWD/build/release/bin/minic-as QEMU_USER=/usr/bin/env WORK_DIR=/tmp/minias-gcc-runtime-ab-current-head-20261102 bash tests/assembler/probe-minias-gcc-runtime-ab.sh x86_64` | `MINIAS_GCC_AB=PASS arch=x86_64 units=2 gcc_source_same=1 strict_elf=2 static_glibc=1 gnu_bfd=1 qemu=2 stdout_equal=1 exit_equal=1` | Genuine x86 GCC `.s`, both AS paths, GNU BFD static glibc, real native execution and equal output; not kernel P1. |
| `MINIAS=$PWD/build/release/bin/minic-as QEMU_USER=/usr/bin/env WORK_DIR=/tmp/minias-cjson-current-head-20261102 bash tests/assembler/probe-minias-gcc-cjson-runtime.sh x86_64` | `CJSON_MINIAS_REAL_RUNTIME=PASS arch=x86_64 units=3 workloads=2 GCC_S=1 MiniAS_only=1 GNU_BFD_static_libc_libm=1 engine=/usr/bin/env GNU_output_identical=1` | Full vendored cJSON source and runtime workloads under native x86. |
| `MINIAS=$PWD/build/release/bin/minic-as WORK_DIR=/tmp/minias-loong-lbt-20261102 LOONGAS=loongarch64-linux-gnu-as LOONGCOPY=loongarch64-linux-gnu-objcopy bash tests/assembler/probe-loong-lbt-gcc-gnu.sh` | `MINIAS_LOONG_LBT_GNU=PASS kernel_probe=1 instructions=4 invalid_rejected=8 GNU_bytes=exact` | Verifies actual Linux `movscr2gr` Kconfig assembler capability and GNU machine-code bytes, **not** Linux final `.config` SHA parity. |
| `PYTHONPATH=/usr/lib/python3/dist-packages MINIAS=$PWD/build/release/bin/minic-as QEMU_USER=/usr/bin/env WORK_DIR=/tmp/minias-gcc-zlib-current-head-20261102 bash tests/assembler/probe-minias-gcc-zlib-runtime.sh x86_64` | `MINIAS_ZLIB_RUNTIME=PASS arch=x86_64 tus=16 gnu_bfd=2 qemu_runs=2 gzip=PASS`; full library's original 15 TUs + one independent caller, GNU/MiniAS 16/16 assembled, same real compression/gzip output | Original pinned zlib 1.3.1 downloaded after local network approval; real x86 native runs (`/usr/bin/env`), **not** QEMU. |
| `PYTHONPATH=/usr/lib/python3/dist-packages MINIAS=$PWD/build/release/bin/minic-as QEMU_USER=/usr/bin/env WORK_DIR=/tmp/minias-gcc-lua-current-head-20261102 bash tests/assembler/probe-minias-gcc-lua-runtime.sh x86_64` | `MINIAS_LUA_RUNTIME=PASS arch=x86_64 units=33/33 bfd_links=2 real_runs=2 output_equal=1 runner=/usr/bin/env` | Full original pinned Lua 5.5 interpreter, both GNU/MiniAS assembly and real native script execution; **not** QEMU. |

Both upstream download attempts initially failed in the restricted network
environment (`browser-proxy:8889` unavailable); authorized network execution
then obtained the pinned sources and the complete zlib/Lua acceptance stages
passed. These runs remain base-binary evidence, not final-SHA Linux P1.

An independently queried GitHub Actions **push** suite at intermediate
`352537a1e60db32ec9f830f2fc672e4fd74b5ac5` adds actual four-architecture
QEMU-**user** evidence:

- [runtime ladder 41450325611](https://github.com/yituanxing/minic-toolchain/actions/runs/41450325611): four target jobs green; staged, cJSON, original full zlib, and original full Lua 5.5 test steps each succeeded on x86/AArch64/RV64/Loong.
- [GCC single-AS A/B 41450325455](https://github.com/yituanxing/minic-toolchain/actions/runs/41450325455): four target jobs green; exact GCC `.s`, GNU/MiniAS, GNU BFD, QEMU-user A/B steps all succeeded.
- [real BusyBox 41450325500](https://github.com/yituanxing/minic-toolchain/actions/runs/41450325500): all four pinned original-source build/execute target jobs green.

The immutable 3536, 3x7, and original Linux P0/P1 workflows at this
intermediate SHA were correctly **skipped** (their opt-in markers were not
present). QEMU-user library/BusyBox success does not replace system-QEMU
Linux P1 or final SHA recertification.

## Actual four-architecture acceptance at `5381dc63`

Commit [`5381dc63ca2334f9235e62ce34946df6f335a2c7`](https://github.com/yituanxing/minic-toolchain/commit/5381dc63ca2334f9235e62ce34946df6f335a2c7)
triggered the four opt-in gates. Unlike intermediate results above, **every
requested heavy `push` workflow completed successfully** at this SHA:

| Independent dimension | Official run | Exact evidence |
| --- | --- | --- |
| Original Linux 6.6.143 GNU Kbuild `.s` → MiniAS-only `.o` → GNU BFD → **system-QEMU P0/P1** | [x86/A64/Loong + GNU controls](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062804), [RV64](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062525) | Original Kbuild owners x86 **2841/2841**, A64 **4171/4171**, Loong **2748/2748**, RV64 **2099/2099**, assembler bypass **0** throughout; each architecture **P0 2/2, P1 12/12** with 22 guest checks and fatal=0. |
| Independent GNU kernel controls and config provenance | Same three-target workflow | x86 GNU baseline also boots P0/P1; A64 and Loong GNU baseline boots P0/P1 and PRE/FINAL `.config` SHA matches corresponding MiniAS build exactly; `MINIAS_LINUX_MATCHED_CONTROL=PASS`. |
| 21 immutable original Linux shards (3×7) | [3×7](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062725) | **21/21 shards + aggregate SUCCESS**, `MINIAS_LINUX_S_STRICT_GATE=PASS candidates=9645` (assembly-only: `GNU_ORACLE=NOT_TESTED P1=NOT_TESTED`). |
| Frozen 3,536 | [3536](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062602) | **11/11 jobs SUCCESS**, `MINIAS_A0_GATE_V1=PASS c_frozen=3352 c149=149 native=35 selected=3536 pass=3536 fail=0`. |
| Original GCC programs, cJSON, full zlib and Lua 5.5 | [QEMU-user runtime ladder](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062722) | All four architectures **4/4 SUCCESS**: 5/5 stages, cJSON 3 units/2 workloads, zlib 16 units, Lua 33 units, GNU/MiniAS real execution equal. |
| GNU/MiniAS GCC A/B and original BusyBox | [A/B](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062619), [BusyBox](https://github.com/yituanxing/minic-toolchain/actions/runs/41453062814) | Both **4/4 architecture jobs SUCCESS**; BusyBox 154 original assembler owners, 10/10 applets, no fallback, QEMU-user. |

One PR-only native Target contract failed in an old test structural matcher
looking for the canonical Section/Symbol record in `minias_internal.h`
after its declaration moved to `minias_object_records.h`. The follow-up
corrects three affected ownership gates and makes the six GNU DataLayout
profiles mandatory in CI; **production assembler code does not change**.
This follow-up must complete its own exact-head CI and optional workflows.

## Final-SHA proof and fail-closed interpretation

1. Record the final `git rev-parse HEAD`, final commit message, and all
   originating GitHub Actions run IDs/job conclusions for that same SHA.
2. Require all three x86/AArch64/LoongArch MiniAS Linux guest markers, the
   same-head GNU controls, and successful `matched-config-provenance`.
   Require RV64 `MINIAS_LINUX_GCC_RUNTIME=PASS p0=PASS p1=12/12` independently.
3. Require the immutable 3x7 aggregator and the 3536 gate success, with
   all source SHA and provenance prerequisites intact. Separately require
   four-architecture GCC/runtime ladder and A/B jobs, or state their exact
   failures; do not silently replace real zlib/Lua with synthetic programs.
4. Preserve Kbuild log, `.cmd` producer census, linker status, QEMU serial
   log and guest P1 checker artifacts; report **NOT_TESTED** if any link or
   boot stage was never reached.

Earlier Linux runs proved x86 (run 41119929978, `ec3a9008`) 2841/2841
production owners and P0/P1, RV64 (41119929731, `ec3a9008`) 2099/2099
and P0/P1, and Loong (41122640321, `cbdb07f1`) 2748/2748 and P0/P1.
But that Loong run **failed matched GNU Kconfig parity** because MiniAS
then lacked `movscr2gr`. The exact-byte local capability probe above now
passes, while complete current GNU/MiniAS `.config` parity remains to be
re-certified. The previous full AArch64 4173-object comparison showed only
9 strict ELF equals, so **guest P1 success is not GNU ELF object equality**;
report these as distinct acceptance dimensions.
