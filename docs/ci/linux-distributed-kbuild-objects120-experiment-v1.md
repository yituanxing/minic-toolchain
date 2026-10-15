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


## Certified 120-object result — 2026-10-08

- **SUCCESS:** [Linux Distributed Kbuild Objects 120 Experiment V1, run 37762351340](https://github.com/yituanxing/minic-toolchain/actions/runs/37762351340), at commit `b42f858e5fa2cfe01806cd61b62b0e486911a3da`. Runtime start 10:16:10 UTC, final result 10:19:05 UTC (~175 s including setup, preparation, independent receiver and upload).
- Six **real Kbuild producer** jobs each compiled 20 selected `.o` files, all success. Their isolated object-target Kbuild stages took 74, 73, 48, 66, 77, 62 seconds respectively (not complete job wall times). Each had exactly the same `.config` digest `e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7` and MiniC binary SHA256 `c1333f97fb2139de7d5272525a384b9c59f87b1dc981c42c4ac18e1a9778c71a`.
- All six compressed object+command artifact bundles were uploaded and restored into a new runner (individual compressed bundles ~1.8–2.7 MB). Receiver verified **240 files**: 120 real RISC-V `.o` and 120 hidden Kbuild `.<name>.o.cmd` files.
- After restoration, a real `make -j4` on the 120 requested targets returned `DIST_O120_REUSE=PASS objects=120 target_recompiles=0 ancillary_minic_calls=3 seconds=3`. The only ancillary compile targets were `scripts/mod/empty.o` and two native RISC-V vDSO source objects; none of the transferred 120 object/command hashes changed.
- RISC-V partial link succeeded: `DIST_O120_PARTIAL_LINK=PASS objects=120 bytes=77224760` (~73.6 MiB).
- The first attempt [37762144585](https://github.com/yituanxing/minic-toolchain/actions/runs/37762144585) failed during prepare due to malformed experimental shell generated in the initial expansion, not MiniC or Linux source errors; repaired in commit `b42f858e5fa2cfe01806cd61b62b0e486911a3da`. Preserve both outcomes as reproducible evidence.
- The earlier [20-object proof](https://github.com/yituanxing/minic-toolchain/actions/runs/37760885838) is a strict subset of these exact 120 explicit targets. Its earlier workflow can be retired byte-for-byte into `.github/workflows-disabled/` while preserving source, runtime evidence and Git history.

**Boundary:** Partial `ld -r` success does not establish final Kbuild archive membership, complete Linux Image linking, modpost, Kallsyms, or QEMU boot. The next step should expand Kbuild-driven object discovery and validate `built-in.a`/generated dependencies before attempting to replace the certified full Image producer.
