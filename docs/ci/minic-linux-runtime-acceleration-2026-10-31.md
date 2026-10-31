# MiniC real Linux runtime: restore RV-style iteration throughput (2026-10-31)

This is a **GNU Linux 6.6.143 runtime** progress and execution ledger, not a
compiler-rt acceptance or a compile-only claim. MiniC compiles **all original
Linux target C translation units**; the original GNU preprocessor, AS,
Kbuild/LD, initramfs, and QEMU remain unchanged. A GNU-compiled C fallback
invalidates full-MiniC certification even when the mixed kernel boots.

## Last actual exact-head full campaign

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
