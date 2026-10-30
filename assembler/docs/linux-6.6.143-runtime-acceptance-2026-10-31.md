# MiniAS Linux 6.6.143 real-runtime evidence (2026-10-31)

## Acceptance contract

The production `.s` files come from GNU GCC/Kbuild. MiniAS, and **only**
MiniAS, replaces the GNU assembler. GNU binutils link the kernel; system
QEMU boots the resulting image. Each target must pass both boot smoke tests
(P0 2/2) and the real initramfs program suite (P1 12/12). A separately
built GNU-as baseline checks the environment, not MiniAS correctness.

Object creation, successful ELF linking, and actual QEMU user-space
execution are separate gates. A simulated fix to a failing guest's RAM
does not count as an accepted native MiniAS output.

## Evidence at the latest x86 repair

- Branch: `refactor/minias-target-isa-abi-core`, PR #667 (Draft).
- Fix commit: `6692f346db0e687ba0b2671178f3a792a9786353`.
- x86 native Linux 6.6.143 run: https://github.com/yituanxing/minic-toolchain/actions/runs/41098360574
- MiniAS assembled **2,984** original x86-64 Linux inputs; GNU linker
  produced `bzImage`; unmodified image booted in QEMU.
- MiniAS lane: **P0 2/2; P1 12/12; runtime checker 22 passed, 0 fatal**.
- Independent GNU-as lane: **P0 2/2; P1 12/12; checker 22 passed, 0 fatal**.
- Job marker: `MINIAS_LINUX_FOURARCH_RUNTIME=PASS arch=x86_64
  p0=2/2 p1=12/12 gnu_baseline=PASS gnu_bfd_link=1 no_gas_fallback=1`.

The x86 blocker was parsed `symbol - 0xffffffff80000000 + PAGE_FLAGS`
as `symbol - (0xffffffff80000000 + PAGE_FLAGS)`, which silently cleared
present/write bits in the early 5-level kernel page tables. The shared
expression parser now evaluates the complete signed constant suffix in
source order. `tests/assembler/probe-x86-kernel-pgt-gnu.sh` verifies GNU
RELA addends and final linked page-table contents, without a test-only
assembler or kernel patch.

## Cross-target status and next gate

- AArch64 and LoongArch64 native Linux P0/P1 were independently green
  in https://github.com/yituanxing/minic-toolchain/actions/runs/41084581222
  on an **earlier commit**. They must be rerun after the shared parser fix.
- The x86/AArch64/LoongArch64 latest-head full matrix is intentionally
  triggered by `[minias-linux-four]` in this evidence commit.
- RV64 Linux runtime has a historical separate `linux-single` acceptance
  lane, with real GCC + MiniAS-only + GNU link + QEMU P0/P1 success at
  https://github.com/yituanxing/minic-toolchain/actions/runs/38159723042.
  Its latest-head rerun is intentionally triggered by
  `[minias-linux-gcc-single]` in the **same** evidence commit. The
  three-target matrix alone does not certify RV64.

This record distinguishes each committed input tree and preserves the
GNU-only tools boundary. Do not report four-target exact-HEAD runtime
success without the corresponding current-head QEMU evidence.

## Strict acceptance quality restoration (October 31)

The older x86 `2984` count is *assembler invocations*, not a manifest of
Kbuild-owned production objects. The later independent production-provenance
checker rejects bypasses and counts actual `.cmd` owners. It separately
classifies disposable assembler feature probes, GNU information queries,
host `objtool`, and link-combined files. A high pass count alone does not
prove every original C/ASM object passed through MiniAS.

Verified acceptance evidence, always tied to its exact code revision:

| Revision / run | Target | Evidence | Result |
| --- | --- | --- | --- |
| `ec3a9008`, [41119929978](https://github.com/yituanxing/minic-toolchain/actions/runs/41119929978) | x86-64 | MiniAS production `.cmd` owners 2841/2841, bypass=0, MiniAS P0 2/2 P1 12/12, independent GNU P0 2/2 P1 12/12 | Runtime PASS; final Kconfig parity not yet tested |
| `ec3a9008`, [41119929731](https://github.com/yituanxing/minic-toolchain/actions/runs/41119929731) | RV64 | MiniAS production 2099/2099, bypass=0, P0 2/2 P1 12/12 | Runtime PASS |
| `cbdb07f1`, [41122640321](https://github.com/yituanxing/minic-toolchain/actions/runs/41122640321) | LoongArch64 MiniAS | Production 2748/2748, bypass=0, Kconfig PRE=FINAL, P0 2/2 P1 12/12 | Runtime PASS; matched GNU config FAIL |
| `cbdb07f1`, [41122640321](https://github.com/yituanxing/minic-toolchain/actions/runs/41122640321) | LoongArch64 GNU | Independent GNU AS, Kconfig PRE=FINAL, P0 2/2 P1 12/12 | Baseline PASS |

For the last two rows, the genuine GNU Kconfig SHA is
`5cd1dcd0cd3db4f8d2713fd3fbd0a550198fd4f0c4598a31b290b1600573b9d2`,
whereas MiniAS' SHA is
`b3335253c0d23be5433d325ab023290243a30fdc1e3e658d24c84429eb018753`.
The complete independent `.config` pair differs exactly in GNU's
`CONFIG_AS_HAS_LBT_EXTENSION=y` capability result and the related unavailable
`# CONFIG_CPU_HAS_LBT is not set` menu entry. Linux 6.6.143 queries the real
instruction `movscr2gr $a0,$scr0`; this is an assembler feature gap, not a
reason to override or exclude Kconfig variables. GNU/MiniAS final-config
parity must remain fail-closed until that instruction is supported and the
pair is independently re-certified.

Likewise, AArch64's last complete 4173-object GNU sidecar comparison had
strict ELF equality in only 9 cases, with 4164 strict differences. A separate
link/runtime `SHF_ALLOC` projection reported 1459 semantically equal objects
and 2714 differing objects (principally relocations), with no inconclusives.
Real QEMU P0/P1 success is evidence of boot and program behavior, **not** a
certificate of GNU ELF or all-loaded-section semantic equality. Preserve
per-object differences and representative original GNU/MiniAS object pairs
for investigation; `GNU_EQ=NOT_CERTIFIED` remains the honest status.

The same-commit Linux release, final resolved Kconfig, original GCC-generated
assembly, initramfs contents, provenance, GNU BFD link and real guest markers
are distinct dimensions. A missing check must be reported as `NOT_TESTED`,
never inferred from the success of an unrelated gate.
