# Linux 6.6.143 RISC-V native vDSO64 / full-union P1 certification

Date: 2026-10-14. Branch: `agent/linux-expanded-kbuild-v0`. Reference commit: `a44067096709ee51db4ed113202ceb4eec0b3af0`.

**Acceptance run:** [Linux Runtime GCC Baseline #38542731056](https://github.com/yituanxing/minic-toolchain/actions/runs/38542731056). Job `gcc-shard-verify` #115781216652: **SUCCESS**. Retained artifact ID `11883705748` includes `cohort.log`, `results.tsv`, and original `native-union/minild.trace`, `minild-times.tsv`, `miniar.trace`, `miniobjcopy.trace`.

## Real Kbuild and runtime evidence

- `NATIVE_UNION_CACHE=HIT`: all 2,064 frozen MiniC C objects SHA-attested, `C_recompiled=0` in the native MiniAS promotion.
- `MINIAS_RUNTIME_POOL=PASS`: 2,060 stable objects reassembled by MiniAS; 4 objects are handled through the **real generated-header dependency** recompile path. Original 34 Kbuild `.S` objects reassembled by MiniAS (`MINIAS_NATIVE_S=PASS`).
- Full actual Kbuild, **not standalone probe**: MiniLD performs the RISC-V ELF64 `vdso.so.dbg.tmp` link from the actual MiniC/MiniAS objects and original `vdso.lds`; MiniObjcopy runs next. Exactly one native vDSO64 target link, no target GNU LD fallback.
- The new real vDSO changes `include/generated/vdso-offsets.h`. Kbuild automatically selects its **exact four** dependent C objects: `arch/riscv/kernel/{alternative,signal,compat_signal,vdso}.o`. No arbitrary restart of the entire 2064-object build.
- Strict resulting ELF verification passes (`MINILD_VDSO64_KBUILD_ABI=PASS`): correct ELF64 RISC-V ET_DYN, all seven `LINUX_4.15` versioned exports, four actual MiniObjcopy calls.
- Real QEMU full-all verified P1: `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`. This is a **12-system-call Python-era oracle**, not a claim of exhaustive Linux testing.
- `MINIAS_ALL_C_RUNTIME=PASS`, `MINIAS_NATIVE_S_INTEGRATION=PASS`, `MINILD_NATIVE_R=PASS`, `MINILD_PURGATORY_R=PASS`, `MINILD_PURGATORY_STATIC=PASS`.
- `NATIVE_UNION_RESULT=PASS pp=GNU minic=2064 from_cache ar=MiniAR_full_actual_Kbuild ar_native_calls=643 ar_mpit_calls=2 ar_gnu_calls=0 ld=MiniLD_certified_boundaries vdso64_native=1 objcopy=MiniObjcopy runtime=P1`.

## Direct target LD provenance audit

Counts taken **from the published, exact successful run artifact**, `native-union/minild.trace` (14 calls, including both actual build/reverification trials):

| Actual LD route | Calls | Certification |
| --- | ---: | --- |
| MiniLD vDSO64 `-shared -T arch/riscv/kernel/vdso/vdso.lds` | 1 | Native real Linux, QEMU P1 |
| MiniLD purgatory relocatable and final static | 1 + 1 | Native real Linux, P1 |
| MiniLD `vmlinux.o -r` | 2 | Native real Linux, P1 |
| MiniLD kernel kallsyms1/kallsyms2/final `vmlinux` | 6 | Native real Linux, P1 |
| GNU LD `--pack-dyn-relocs=relr` temporary detection | 1 | Capability probe, **not an actual Linux output** |
| GNU LD `-z pack-relative-relocs` temporary detection | 1 | Capability probe, **not an actual Linux output** |
| GNU LD `arch/riscv/kernel/compat_vdso/compat_vdso.so.dbg.tmp` | **1** | **Remaining real GNU target link: ELF32 compatibility vDSO** |

**Do not claim zero remaining GNU target LD calls.** Two temporary GNU invocations are feature detection and do not generate installed target ELF. One actual target ELF32 `compat_vdso.so.dbg.tmp` link still uses GNU LD; it is also not acceptable to hide this fact simply because QEMU P1 passes.

## Next certification boundaries (ordered)

1. **Compat vDSO32:** Reproduce the exact cached four-object `-shared -melf32lriscv -T arch/riscv/kernel/compat_vdso/compat_vdso.lds` command. Implement proper ELF32 shared output and script/PHDR/version handling in MiniLD; do not merely accept `-melf32lriscv` then emit ELF64. Compare with GNU's actual ELF32 output. Require genuine Kbuild link, zero GNU compat-vDSO fallback, then repeat cached QEMU P1. Existing MiniLD currently restricts emulation to `elf64lriscv`.
2. **MiniPP full-Kbuild integration:** Keep a fixed GNU preprocessor baseline while certifying each native target tool boundary. MiniPP independent preprocessing equivalence tests are not a substitute for real Kbuild P1. Once the target link boundary is certified, replace GNU PP, retain SHA-based cache rules and rerun QEMU P1.
3. **Bootstrapping:** Only after complete runtime certification. Host build support tools (HOSTCC) are explicitly outside claims about target toolchain independence.

## Process controls

- Reuse golden GNU Linux source/config and frozen 2064 MiniC object / seven compilation-shard caches; only rebuild exactly the dependency-affected objects.
- Log real native and GNU target invocations; maintain explicit capability/unsupported gates.
- No additional workflow or job for this audit. A standalone green link proves only linker behavior; complete QEMU P1 is needed for integrated acceptance.
- Most recent native wrapper fix `a440670` removed erroneously duplicated commands outside the emitted `SH_LD` heredoc and added `bash -n` on all emitted wrappers, preventing another false failure during the prepare stage.
