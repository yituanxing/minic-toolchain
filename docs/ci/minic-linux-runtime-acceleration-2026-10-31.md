# MiniC real Linux runtime: restore RV-style iteration throughput (2026-10-31)

This is a **GNU Linux 6.6.143 runtime** progress and execution ledger, not a
compiler-rt acceptance or a compile-only claim. MiniC compiles **all original
Linux target C translation units**; the original GNU preprocessor, AS,
Kbuild/LD, initramfs, and QEMU remain unchanged. A GNU-compiled C fallback
invalidates full-MiniC certification even when the mixed kernel boots.

## Later real QEMU result — supersedes the link failures below

On 2026-10-31, [three-ISA run #41181106144](https://github.com/yituanxing/minic-toolchain/actions/runs/41181106144)
ran **all 21 fresh MiniC/GNU-AS producer shards** and all three real GNU
relink/QEMU receivers. The receiver artifacts and job logs record:

| Target | Authenticated MiniC C owners | GNU LD + MiniC Image QEMU P0/P1 | Full GCC option parity |
| --- | ---: | --- | --- |
| x86-64 | 2758/2758 | **PASS**, GNU/MiniC runtime differential PASS | **INCONCLUSIVE**, 46 unqualified distinct options |
| AArch64 | 4108/4108 | **PASS**, GNU/MiniC runtime differential PASS | **INCONCLUSIVE**, 40 unqualified distinct options |
| LoongArch64 | 2723/2723 | **PASS**, GNU/MiniC runtime differential PASS | **INCONCLUSIVE**, 34 unqualified distinct options |

The authenticated `candidate/boot/minic-qemu-receipt.json` for all three
targets reports `MINIC_QEMU_P0_P1_PASS_NO_GCC_C_FALLBACK`, and the independent
P0/P1 differential reports `MINIC_LINUX_RUNTIME_DIFF=PASS`. The strict
receivers intentionally return failure because `-O*`, stack protector,
automatic variable initialization, architecture security/ABI flags and debug
options have **not** all been shown equivalent to GNU. Do not erase that
distinction or waive the gate merely because the kernel boots.

Earlier `#41090926953` undefined assertions and LoongArch B26 branch-range
failures below are **historical** blockers resolved sufficiently for this
newer real run; they are not the last observed Linux boot result.

RV32's independent [run #40268099875](https://github.com/yituanxing/minic-toolchain/actions/runs/40268099875)
already demonstrated **1741/1741**, genuine ELF32 GNU Image and 12/12
independent P1 boots on the same SHA. RV64's historical
[run #39063287653](https://github.com/yituanxing/minic-toolchain/actions/runs/39063287653)
demonstrated **2064/2064** Stage4 GNU-linked Linux and P1 12/12. All five
receipts are **historical exact-commit proofs**, not a certification of the
2026-11-03 IR-P28 refactor head `90fe3b8`.

At that refactor head, [three-ISA run #41640145554](https://github.com/yituanxing/minic-toolchain/actions/runs/41640145554)
reported green overall but executed **only the trigger gate**; all 21 shards,
GNU LD and QEMU receiver jobs were skipped. Native strict workflow
`#41640145565` failed at the **pre-existing** backend-review manifest drift,
before executing its runtime stages. These are not new compiler Linux faults.

## Earlier full campaign — linker blockers before #41181106144

[`#41090926953`](https://github.com/yituanxing/minic-toolchain/actions/runs/41090926953),
commit `83d6a53ad1b1e3e4ab52bc87e0ab714ada276a96`: three `gnu-boot` jobs
**ran**. Original GNU QEMU P0/P1 passed **2/2 in each target**. Every MiniC
C owner assembled with GNU AS; final original GNU LD failed in each target,
so **no MiniC candidate QEMU boot** was executed.

| Target | MiniC C -> GNU AS | Real GNU LD remaining | Previous batch |
|---|---:|---|---|
| x86-64 | 2758/2758 | `__compiletime_assert`: 5 references, 5 symbols | 11 -> 5 |
| AArch64 | 4108/4108 | `__compiletime_assert`: 1 reference, 1 symbol; GOT/PLT errors gone | 13 -> 1 |
| LoongArch64 | 2723/2723 | `R_LARCH_B26` one out-of-range `head.o` -> `start_secondary` | 1 -> 1 |

GNU Kbuild used the frozen genuine original `.o.cmd`/`.i` inputs. The
`link-failure-owner-diagnostics/` artifact contains **5/5** x86 and **1/1**
AArch owner traces. A failed external artifact ZIP download does not make the
owners available locally; do not infer a particular `_NNN` assertion from
older run logs without authentic source/ELF matching.

Loong's last measured B26 displacement was `-144063268` bytes. The signed
26-bit branch range is approximately +/-128 MiB; the last measurement exceeds
it by `9845540` bytes (~9.39 MiB). This is a whole-kernel code layout issue,
not an undefined symbol. No unmeasured linker-range shortcut is acceptance.

## What made the earlier RISC-V campaigns fast

- Historical RV64 Linux success: `2064/2064` original C owners, GNU AS/LD,
  real QEMU P1 `12/12`; seven independent parallel runners took 42-74s each,
  with ~7 minutes for the complete campaign on the historical configuration.
- Freeze `.i`, genuine `.o.cmd`, GNU original ELF `.o`, Linux SHA/Kconfig,
  owner lists and compiler identity **once**; cache content by exact SHA.
- Edit the compiler, recompile only affected frozen owners in seconds,
  GNU-assemble and compare `nm -u`; collect **every** new failure before a
  consolidated root-cause repair.
- For fault isolation, re-link genuine Kbuild with a labeled *mixed GNU/MiniC*
  C-owner set and divide into seven cohorts, then smaller owner groups.
  Mixed results are diagnostic, **never** final acceptance.
- Only after local proof, run all-MiniC/no-fallback original GNU final LD,
  Image and real QEMU P0/P1 to certify each candidate SHA.

RV32's historical 56-cohort fault census and partial fixes did **not** make
its final Image QEMU P0 pass; never merge RV32 partial progress into RV64
success numbers.

## Why current three-ISA CI is slower

Each PR's full Linux campaign starts by rebuilding the original source,
compiler metadata, **31.626 GB apparent three-ISA GNU golden worktrees**,
and then recompiling all 2758+4108+2723 C owners in one machine per ISA.
Individual GNU baseline builds took approximately **12m41s x86, 15m54s
AArch, 16m51s Loong** in the last sampled campaign, before MiniC/AS/LD.
The historical immutable-cache/fast-replay mechanism was not wired into this
new multi-ISA workflow. More coding agents alone cannot remove that tax.

## Current speedup implementation and truth conditions

1. `tools/dev/minic-linux-quick-replay.py` reads SHA-authenticated frozen
   owner archives, uses the **same** GNU Kbuild MiniC/AS flag verifier as the
   full candidate runner, and can run MiniC -> GNU AS -> `nm -u` before/after
   a compiler change. The standalone synthetic x86 flags, GNU AS and GNU
   `-O2` differential gate takes under one second locally. It is not LD/QEMU.
2. `tools/ci/linux-three-isa-minic-candidate-v1.py` now prints bounded
   `symbol -> authenticated ELF C owner -> frozen .i/.cmd SHA` mappings in
   the CI console on actual GNU LD failure; unknown/multiple matches remain
   explicit. This avoids downloading a blocked ZIP merely to discover a bug.
3. `tools/ci/linux-three-isa-gnu-cache-verify-v1.py` independently verifies
   an original GNU golden restoration: source/config/Image SHA, original
   ELF C-owner universe, frozen `.i/.cmd`, ABI, and current absolute `O=`
   paths. It does **not** accept a cached old QEMU receipt as a new boot.
4. The three-ISA workflow has an opt-in bounded zstd size/CPU pilot and
   opt-in original GNU cache restore/save integration. Never silently turn
   on multi-gigabyte caching without inspecting actual upload size, retention
   and cache hit data; the normal PR path must stay fully operational.

## Next experimental gates

- Report real zstd compressed bytes/time per ISA, then enable exact-key
  read-only-GNU cache reuse when it is within GitHub cache capacity. No
  broad `restore-keys`; a stale/foreign compiler or `O=` fails closed.
- After a true hit, still generate a **fresh** GNU baseline P0/P1 receipt,
  then replace **all** original target C `.o` with MiniC/GNU-AS `.o` before
  GNU Kbuild relink. Never save the post-overlay cache.
- Fix all authenticated residual x86/AArch assertion owners in one grouped
  code batch. Loong code-size work should target shared register residency
  and the measured `head.o`/`start_secondary` relative displacement, not
  arbitrary absolute ELF bytes or edited kernel source.
- For each real rerun, use the mandatory last-commit trigger file and check
  **three non-skipped jobs, actual GNU LD, actual candidate Image/QEMU**.

All duration and size numbers above are historical measurements, not a
promise that the restored pipeline has already reached RV64 throughput.
