# Experimental distributed complete Image and QEMU pipeline

The separate graph preflight identifies the Kbuild C translation-unit candidate set
from pinned Linux 6.6.143 RISC-V runtime config, with exact manifest/config hashes.
It is **not** a proof of complete link-closure membership: Linux also builds .S,
generated headers, thin archives, kallsyms and linker scripts.

This workflow first reproduces that plan, then distributes its C object paths
across seven fresh GitHub-hosted runners. Each uses the existing canonical **39-patch**
MiniC profile and the real stage2 GCC-preprocess/MiniC/GNU-assembler Kbuild CC wrapper.
Each exports every `.o` plus hidden `.<name>.o.cmd` and exact SHA256, compiler,
config, manifest identities. Failure to produce even one planned object fails the shard.

A fresh receiver checks every file, then invokes the **real** `make ... Image` (not a
synthetic `ld -r` partial link) to build archives, vmlinux, System.map and Image.
It verifies SHA256 identity and zero recompilation of transferred targets.
Only then does it invoke the same 90-second QEMU runtime-boot marker probe as the
existing independent canonical Linux Image workflow.

This is an *experiment* and must not displace the existing certified single-runner
Image producer or runtime gates. The corpus graph, Kbuild generated products and
the built-in archives may expose real new blockers. A failed shard, final link or
QEMU marker is a genuine failure and cannot be counted as success.

Stop and inspect evidence if: manifest differs, shard compilation fails, transfer
identity mismatches, any requested object is recompiled on the receiver, the
Image is missing/invalid, or QEMU fails. The first phase is a full graph gate,
not a claim that Linux now boots.
