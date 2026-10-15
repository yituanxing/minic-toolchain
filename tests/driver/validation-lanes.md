# Independent MiniC driver validation

The unified `minic` executable routes distinct frontend and ELF tool
stages; passing its integration test does not imply that the compiler,
preprocessor, assembler, archiver or linker passes the full Linux runtime
contract. Avoid silently counting one combined smoke as independently
certifying its components.

- Maintained test owner: `.github/workflows/minic-driver-v0.yml`.
- Current opt-in push gate: `[minic-driver-v0]`; this directory is on its
  declared trigger path list.
- Its current integration uses a pinned external GNU RV64 compiler to build
  musl, runs the existing driver-routing and dynamic-library tests, and
  preserves the exact tested HEAD and trace.
- Treat a green driver run as an orchestration/interface proof at precisely
  its tested scope; follow standalone GNU-baseline tool-specific contracts
  before native components can be promoted together.
- Self-hosting and missing MiniObjdump implementation are intentionally out
  of scope for this validation stage.

Do not add another GitHub Actions workflow for this owner.

## Reproduced real QEMU defect and focused fix — 2026-10-11

Driver [#38150108182](https://github.com/yituanxing/minic-toolchain/actions/runs/38150108182)
**failed** at the actual linked PIE runtime (`qemu-riscv64` SIGILL / exit 132).
The GNU-readelf artifact shows `main=0x1059`, an **odd instruction
address** on RISC-V; the preceding ELF .rodata has an odd size.
This is an actionable ELF section-alignment defect, not an apparent driver
option-parsing failure. The MiniAS `.section .text.main,"ax"` handler
advertised input-section alignment 1 rather than the 4-byte instruction
alignment used for the normal `.text` directive.

The minimal independent `tests/assembler/run-a0.sh` test first failed
with `MINIAS_EXEC_SUBSECTION_ALIGN=FAIL actual=1 expected=4` in
[run #38150580702](https://github.com/yituanxing/minic-toolchain/actions/runs/38150580702).
The fix changes only executable `.section` input section alignment,
and the focused independent MiniAS run
[#38150684714](https://github.com/yituanxing/minic-toolchain/actions/runs/38150684714)
is green.

This driver test now reruns the **same** dynamic musl / mini-CPP/CC/AS/LD
program and executable without relaxing its original functional assertions.
A green run will validate the repaired joint driver pipeline, not
independent end-to-end tool correctness.
