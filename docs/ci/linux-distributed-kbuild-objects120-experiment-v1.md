# Six-runner distributed Linux Kbuild — 120 true objects (2026-10-08)

A new, **isolated experiment** based on the successful 20-object cross-runner proof
([run 37760885838](https://github.com/yituanxing/minic-toolchain/actions/runs/37760885838)).
This experiment **does not** alter the certified Linux Image producer, cache keys, QEMU tests,
or the original 20-object CI.

## Coverage

- **120 unique, explicit Linux 6.6.143 C targets:** 73 existing canonical owner-cohort targets plus 47 named kernel/MM/FS/RCU/lib/block objects.
- Six independent Ubuntu 24.04 runners, **20 .o files per runner**, using the existing *canonical 39-patch MiniC runtime profile* and `stage2_kbuild_cc.sh` under real `make ARCH=riscv O=... CC=... -j4`.
- Each producer prepares the same pinned kernel tarball (SHA256 verified), defconfig and Kbuild generated prerequisites, and exports `.o` plus hidden `.<object>.cmd`.
- Receiver independently reproduces the same sources/config/compiler binary, verifies all 240 file hashes and six exact target manifests, overlays the 120 object/command files and invokes real Kbuild on all 120 targets.
- PASS requires **zero recompilations of transferred targets** and unchanged SHA256 of every transferred `.o` and `.cmd`. Recompilation of unrelated build-preparation targets is tracked, not misclassified.
- Performs a RISC-V `ld -r` partial link and checks ELF architecture; this is **not** a full Linux Image, modpost, built-in.a, Kallsyms or QEMU certification.
- Fail-closed: incomplete file sets, duplicate/absent sources, missing artifacts, failed Kbuild, changed content, missing metadata or compiler/config differences must stop the run.
- CI is push-path-scoped to the new workflow/manifest/script (or `workflow_dispatch`); no 55-minute full-Image rebuild is triggered.

## Historical evidence and exit criteria

The small run 37760885838 was successful: 20 objects transferred with matching `.cmd`, 0 target recompilations, and a valid RISC-V partial link. This is the next scale experiment.
After a green 120-object run, decide whether to expand to a dependency-complete Kbuild object/archives producer and full Image linker using strict current-profile identity. Do not call a partial link proof full Linux boot.
