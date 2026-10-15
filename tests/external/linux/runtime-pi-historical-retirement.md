# Historical PI runtime lane retirement

This cleanup retires two September PI/SATP runtime diagnostics whose durable questions are now covered by the maintained compiler-semantics and SATP-refresh contracts.

## linux-pi-fast-oracle-v0.yml

This workflow was created while the SATP lowering bug was still active. It combined three diagnostic experiments:

1. run the SATP inline-asm micro contract;
2. rebuild the RISC-V PI startup objects with MiniC;
3. replace only `arch/riscv/mm/init.o` with a GCC-built oracle and retry early QEMU boot.

Its only revision was commit `2fef423c0e31d88cd0419472201a92522c5b463b`. GitHub Actions run `34957215505` completed SUCCESS as a diagnostic workflow, but its internal evidence showed the historical broken state:

- `SATP_WINDOW_MINIC_RC=1`
- `SATP_WINDOW_MINIC_STACK_TOUCHES=20`
- `GCC_INIT_ORACLE=1`
- `LINUX_BANNER=0`

The GCC `init.o` swap therefore did not establish a working Linux runtime; it only helped localize the then-current failure.

## linux-pi-minic-early-v1.yml

This later workflow rebuilt the early PI owner set through MiniC after the first SATP lowering fix and ran a fast all-MiniC early QEMU gate.

Its final revision was commit `a0f0e248e1a7c9bb9d8f66ea07bc21d274a86cc9`. GitHub Actions run `34987975828` completed SUCCESS and showed that the compiler-level SATP micro contract had become stack-free:

- `SATP_WINDOW_MINIC_DIRECT_STACK_TOUCHES=0`
- `SATP_WINDOW_MINIC_LOCAL_STACK_TOUCHES=0`
- `SATP_WINDOW_MINIC_DECL_STACK_TOUCHES=0`
- `PASS compiler/c0/gnu_inline_asm_satp_window_rv64 ... stack_free=1`

but its historical early runtime still recorded:

- `LINUX_BANNER=0`

This was an intermediate frontier, not a current canonical runtime gate.

## Maintained compiler-semantics contract

`.github/workflows/linux-runtime-compiler-semantics-v1.yml` now owns the compiler-level SATP and PI-addressing semantics.

Canonical run `37460113388` completed SUCCESS:

- SATP micro lane: all three SATP variants reported zero stack touches and the stack-free contract passed;
- PI local-symbol lane: the current PI addressing policy was applied and its runtime replay completed with an accepted oracle verdict.

## Maintained real-owner SATP contract

`.github/workflows/linux-runtime-satp-refresh-v0.yml` owns the real current Linux PI/SATP owner question.

Canonical run `37463405710` completed SUCCESS and established:

- PI-only differential localized the expected intermediate regression to `set_satp_mode`;
- real current `arch/riscv/mm/init.o` was rebuilt through MiniC;
- real current `arch/riscv/kernel/setup.o` was rebuilt through MiniC;
- `SATP_WINDOW=PASS`;
- `REAL_SATP_WINDOW=PASS`;
- relinked current-owner runtime reached `FRONTIER_VERDICT=FRONTIER_PASS`;
- highest certified progress reached `init-irq` with pass marker `mm_core_init`.

This is strictly later and more direct evidence than the September fast-oracle and MiniC-early experiments.

## Retirement decision

The following historical YAML files are preserved byte-for-byte under `.github/workflows-disabled/` and removed from the active Actions set:

- `linux-pi-fast-oracle-v0.yml`
- `linux-pi-minic-early-v1.yml`

`linux-expanded-pi-runtime-v0.yml` remains active for now because its final recorded run `34950464878` concluded FAILURE; it will not be retired until that historical failure is either explicitly superseded or its unique contract is migrated.

No historical commits, workflow runs, artifacts, or YAML are deleted.
