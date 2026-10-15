# MiniAS A0 Gate V1 contract

This workflow is the canonical hard acceptance gate for the Linux RISC-V MiniAS A0 corpus.

## Accepted workload

The gate requires all three cohorts at the same repository HEAD:

- 3352 frozen MiniC-generated Linux C translation units, split into the seven certified frozen corpus shards.
- 149 ground-truth C translation units that are present in the successful Linux 6.6.143 RISC-V defconfig build but absent from the historical frozen3352 corpus.
- 35 native Linux RISC-V assembler inputs.

The hard total is therefore 3536 assembler inputs.

## Fail-closed rules

The 3352 cohort must verify frozen corpus identity, rebuild the current MiniC and MiniAS, regenerate every assembly input, and assemble every object as ELF64 RISC-V ET_REL. The aggregate must report pass=3352, fail=0, error=0.

The C149 cohort must verify the frozen C149 corpus, replay all 149 inputs through the current MiniC, and require chain_pass=149 with compile_fail=0 and assemble_fail=0.

The native35 cohort must not depend on the expired historical native26 Actions artifact. It reconstructs the certified 35-target workload from Linux 6.6.143 RISC-V defconfig Kbuild object commands in the same run, preprocesses the exact .S commands to .s, builds the current MiniAS, and requires pass=35 and fail=0.

The final gate succeeds only when all3352, c149, and native35 succeed at the same HEAD, yielding selected=3536 pass=3536 fail=0.

## Cleanup consequence

After this contract is independently green, the historical component workflows
`minias-a0-all3352.yml`, `minias-a0-c149.yml`, `minias-a0-native35.yml`, and
`minias-a0-all3536-aggregate.yml` are redundant hard-gate wrappers and may be archived without deleting their YAML history.

Diagnostic, inventory, semantic-oracle, and focused workflows remain separate until their distinct contracts are audited.
