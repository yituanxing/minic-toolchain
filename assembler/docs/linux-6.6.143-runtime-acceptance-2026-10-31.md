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
