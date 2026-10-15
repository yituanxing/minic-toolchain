# Rehome M0 contracts to Runtime branch (2026-10-08)

From `d2011ef9e8652dc6069b279bccc41a64f4c22043`: `toolchain-m0-structure.yml` used to run on already deleted `agent/ci-runtime-cleanup-v1` and `toolchain/**` refs. On current runtime it therefore had no automatic owner. This is fixed without adding a new workflow.

- Fast invariant tests (`tools/ci/test_linux_runtime_object_set_v1.py`, `tools/ci/test_linux_runtime_prefix_bisect_v1.py`, Stage2 Kbuild wrapper contract, BusyBox Kbuild ownership contract) now run on relevant source/test/workflow path changes to `agent/linux-expanded-kbuild-v0`.
- Four historical `make clean`/frozen compiler boundary checks remain available through `workflow_dispatch` on non-runtime refs and retain their previously intended isolation from the runtime profile.
- This does **not** replace Linux Image/QEMU or full3352 certification, and does not change source or cache IDs.
