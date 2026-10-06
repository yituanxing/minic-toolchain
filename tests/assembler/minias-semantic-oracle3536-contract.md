# MiniAS semantic oracle 3536 contract

`.github/workflows/minias-semantic-oracle3536.yml` is the full semantic-classification surface for the certified MiniAS Linux RISC-V workload.

It classifies the same 3536-input workload shape used by the canonical hard gate:

- 3352 frozen MiniC-generated Linux C translation units across seven certified shards.
- 149 ground-truth C translation units.
- 35 native Linux RISC-V assembler inputs reconstructed in-run from exact Linux 6.6.143 Kbuild object commands.

For each input, GNU as and the current MiniAS are compared with `tests/assembler/elf_semantic_compare.py`. `PASS` and `DIFF` are both semantic classification outcomes; the workflow fails closed on missing classifications, GNU assembly failure, MiniAS assembly failure, or oracle infrastructure error.

The native35 lane must remain self-contained and must not depend on expired historical Actions artifacts. A focused `[minias-semantic-native35]` trigger certifies that lane independently; `[minias-semantic-oracle3536]` certifies the complete aggregate at one repository HEAD.
