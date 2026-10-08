# Linux distributed Kbuild objects — 20-object bounded experiment

This is a **diagnostic**, not a replacement for the canonical Linux Image, QEMU, or 3352-TU certification.

- Two independent Ubuntu 24.04 runners prepare pinned Linux 6.6.143 (tarball SHA256 verified), RISC-V defconfig and the exact MiniC runtime profile.
- Each compiles **10 real Linux C objects using Kbuild with the existing MiniC/GCC wrapper**, not bare `.i -> .s` replay.
- Each exports the `.o` **and hidden `.<name>.o.cmd`** file, hashes, configuration digest, compiler binary digest and exact target list as GitHub Actions artifacts.
- A third, fresh runner independently prepares the same source, profile and config, verifies all producer hashes, restores objects/metadata to the same paths, and invokes Kbuild on all 20 targets.
- **PASS requires that Kbuild performs zero MiniC recompilations and leaves every restored `.o` and `.cmd` bit-for-bit unchanged.** A failure is actionable evidence of mtime, dependency, command, environment or artifact-restore incompatibility.
- Only after passing Kbuild reuse, combine the 20 relocatable objects with GNU `ld -r`. This does **not** establish full Linux Image linkability or QEMU boot behavior.
- No cache producer/consumer or existing runtime workflow is modified. No 55-minute full-image test is triggered. This isolated workflow is auto-triggered only when one of its three experiment inputs changes.
- After diagnosis, archive/retire this experiment according to the CI ownership map; promote a full distributed Image approach only after real Kbuild dependency/metadata correctness and end-to-end certification.

All objects come from 20 targets already present in `linux-expanded-owner-targets-v1.txt`. No GCC C-compile fallback is enabled beyond existing wrapper delegation for non-C tasks.
