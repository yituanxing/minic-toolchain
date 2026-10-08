# Receiver-only recovery of certified distributed 3352 C objects

Original seven-shard full-Kbuild run [37767282927](https://github.com/yituanxing/minic-toolchain/actions/runs/37767282927)
produced all **3352 / 3352** real RISC-V C `.o` artifacts with hidden Kbuild `.cmd`
files, across 7 successful producers. Runtime 39-patch + `constant-p ICE` compiler binary
digest: `c1333f97fb2139de7d5272525a384b9c59f87b1dc981c42c4ac18e1a9778c71a`.
Exact RISC-V `.config` digest: `e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7`.

The **original receiver failed at its config gate before downloading shards**.
Its generated config hash was `eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe`,
while all seven producers and the graph had `e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7`.
This is a *receiver environment/config mismatch*, not a failure of the 3352
completed C-object builds. One possible cause is that the original receiver
installed extra QEMU/cross-libc prerequisites **before** Kbuild `defconfig`.
That cause is only a hypothesis until separately reproduced.

The isolated reuse workflow downloads the **immutable artifacts from run 37767282927**
using `actions/download-artifact@v4` with an explicit `run-id` and
`GITHUB_TOKEN actions:read`. No recompilation of 7x479/478 targets is requested.
It first installs precisely the same prerequisites as the successful producer
runners, prepares Linux and MiniC, and demands SHA equality *before* Kbuild.
Only after restored-object SHA validation and the final true `make Image` does it
install QEMU prerequisites and attempt the RISC-V boot marker.

All prior guards in `linux-distributed-full-image-v1.sh` remain strict:
manifest, compiler, config, source/object `.cmd` SHA, zero transferred-target
rebuild, final vmlinux/System.map/Image, and QEMU. A failed stage is a real
failure and does not certify Linux boot.

Once the receiver experiment is certified, fold its environment isolation into
the canonical distributed Image workflow and retire this one-shot receiver
without losing its CI evidence. Do not change the independent production Image
or QEMU runtime certification.
