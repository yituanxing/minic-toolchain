# Linux runtime PI/SATP differential contract

`.github/workflows/linux-runtime-satp-refresh-v0.yml` is the canonical real-Kbuild PI/SATP isolation workflow.

It preserves three historical diagnostic responsibilities in one ordered run:

1. Rebuild the true `arch/riscv/kernel/pi/cmdline_early.pi.o` owner through the current centralized MiniC runtime profile, retain preprocessed/assembly/ELF relocation/symbol evidence, and prove the Kbuild wrapper actually regenerated the MiniC stage-2 assembly.
2. Relink and run the PI-only image before touching the SATP owner. The current isolation contract requires this checkpoint to classify as `REGRESSED` with first fault owned by `set_satp_mode`; this preserves the historical PI-owner-only differential rather than hiding it.
3. Rebuild `arch/riscv/mm/init.o` and `arch/riscv/kernel/setup.o`, verify the critical SATP CSR window has no stack-memory access between the transition CSR operations, and additionally preserve the historical real-init assertion that no instruction in the source-level CSR window references `sp` at all; then relink and run the final runtime frontier oracle.

## Canonical certification

GitHub Actions run `37458871590` executed on `agent/linux-expanded-kbuild-v0` after installing the upgraded workflow in the canonical cache scope.

The run completed successfully and independently passed:

- certified full fixture, Linux source, and frozen linker restores;
- exact runtime MiniC profile construction;
- true PI owner regeneration with the retained fresh-owner evidence;
- the PI-only differential checkpoint;
- SATP owner and setup owner regeneration;
- the critical `set_satp_mode` CSR-window check;
- early relink and the final QEMU frontier oracle;
- evidence upload.

The upgraded chain therefore supersedes these standalone historical workflows, which may be archived without deleting their YAML history:

- `linux-runtime-pi-fresh-owner-v0.yml`
- `linux-runtime-pi-owner-refresh-v0.yml`

`linux-pi-minic-early-v1.yml` remains separate because it refreshes seven PI startup objects and runs a raw all-MiniC early-QEMU gate rather than this owner-isolation contract.


## Real-init gate consolidation

The historical `linux-pi-real-init-gate-v0.yml` rebuilds the same real `arch/riscv/mm/init.o` owner and rejects any `sp` reference between the SATP CSR transition operations. That stricter source-level assertion is now preserved verbatim inside the canonical SATP refresh workflow in addition to its existing object-level stack-memory check.

Canonical certification run `37463405710` on `agent/linux-expanded-kbuild-v0` completed SUCCESS after the stricter assertion was folded in. It passed the real PI owner rebuild, PI-only differential, real `init.o` and setup owner rebuilds, both SATP-window assertions, early relink, final QEMU frontier oracle, and evidence upload. Therefore the standalone `linux-pi-real-init-gate-v0.yml` is superseded and may be archived without losing its diagnostic invariant.
