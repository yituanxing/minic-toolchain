# Linux distributed Kbuild objects — 20-object bounded experiment

This is a **diagnostic**, not a replacement for the canonical Linux Image, QEMU, or 3352-TU certification.

- Two independent Ubuntu 24.04 runners prepare pinned Linux 6.6.143 (tarball SHA256 verified), RISC-V defconfig and the exact MiniC runtime profile.
- Each compiles **10 real Linux C objects using Kbuild with the existing MiniC/GCC wrapper**, not bare `.i -> .s` replay.
- Each exports the `.o` **and hidden `.<name>.o.cmd`** file, hashes, configuration digest, compiler binary digest and exact target list as GitHub Actions artifacts.
- A third, fresh runner independently prepares the same source, profile and config, verifies all producer hashes, restores objects/metadata to the same paths, and invokes Kbuild on all 20 targets.
- **PASS requires that Kbuild performs zero recompilations of the 20 transferred `.o` targets and leaves every restored `.o` and `.cmd` bit-for-bit unchanged.** Unrelated preparatory targets (such as `scripts/mod/empty.o` or native vDSO) can be rebuilt on the independent receiver; they are reported separately and are not a transfer failure. The first experimental run exposed exactly this distinction, with 3 ancillary MiniC calls while all 20 transferred object and command hashes remained unchanged. A transferred-target mismatch or recompilation is still a hard failure.
- Only after passing Kbuild reuse, combine the 20 relocatable objects with GNU `ld -r`. This does **not** establish full Linux Image linkability or QEMU boot behavior.
- No cache producer/consumer or existing runtime workflow is modified. No 55-minute full-image test is triggered. This isolated workflow is auto-triggered only when one of its three experiment inputs changes.
- After diagnosis, archive/retire this experiment according to the CI ownership map; promote a full distributed Image approach only after real Kbuild dependency/metadata correctness and end-to-end certification.

All objects come from 20 targets already present in `linux-expanded-owner-targets-v1.txt`. No GCC C-compile fallback is enabled beyond existing wrapper delegation for non-C tasks.


## Verified evidence — 2026-10-08

### First run, diagnostic: [37760417834](https://github.com/yituanxing/minic-toolchain/actions/runs/37760417834)

- Both 10-object producers passed, and the receiver verified SHA256 for all **20** transmitted objects and all **20** hidden `.o.cmd` files.
- Receiver's overly broad test rejected three unrelated freshly generated Kbuild support targets (`scripts/mod/empty.o`, native vDSO `vgettimeofday.o` and `hwprobe.o`), while all 20 transmitted object/cmd hashes remained identical.
- Corrected the assertion: no **transferred target** may be recompiled; unrelated support work is reported separately, not silently ignored.

### Certified small experiment: [37760885838](https://github.com/yituanxing/minic-toolchain/actions/runs/37760885838)

- **SUCCESS** on the independent receiver and both producers; no 55-minute full Linux Image build.
- Independent producers generated 10 genuine MiniC Kbuild `.o` each (approximately **25 s** and **26 s** for the Kbuild target step, respectively).
- Exact `.config` digest identical across runners: `e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7`.
- MiniC binary SHA256 identical across runners: `c1333f97fb2139de7d5272525a384b9c59f87b1dc981c42c4ac18e1a9778c71a`.
- The independent receiver restored objects *and hidden command metadata*, verified file hashes, reran real Kbuild and recorded `DIST_O20_REUSE=PASS objects=20 target_recompiles=0`. Some unrelated preparatory targets were built on the fresh runner.
- GNU RISC-V relocatable partial link also passed: `DIST_O20_PARTIAL_LINK=PASS objects=20 bytes=8826256`.
- **Scope limitation:** this proves bounded cross-Runner object reuse and a partial link, NOT full Kbuild Image, Kallsyms, modpost, full ABI correctness or QEMU boot. A larger dependency-complete experiment and eventual full Image end-to-end certification remain required.

### Engineering follow-ups

1. Separate reusable generated-preparation outputs from owner `.o/.cmd`, and retain exact config/profile/compiler/source identity.
2. Increase from 20 to 100+ nonoverlapping selected objects, including built-in archives and generated objects, before attempting a fully distributed Image.
3. Make each object set independently verifiable and fail-closed on a rebuilt or mismatched transferred target; preserve original full Image/QEMU certification in parallel.
