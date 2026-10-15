# Linux full3352 input-integrity rerun, 2026-10-08

Earlier run 37760972924 was falsely green: **3350 PASS / 2 FAIL** (serio.i 2768 and libps2.i 2770). Exit-code propagation has already been fixed in commit e2a9e330.

Focused run 37763934257 materialized only those two files with Kbuild -save-temps=obj -j4; the resulting .i contained corrupted tokens (sdren, __attribute_ection__, xit;;) and GCC -x cpp-output -fsyntax-only rejected serio.i. The captured first invalid artifact is 11543447237.

The second focused run 37764301563 materialized the same paths using Kbuild -j1: **GCC syntax accepted both**, and all 10 cases (five compiler variants times two Linux inputs) compiled. In both cases the resulting .i file sizes were identical for each path, but SHA256 and contents differed. First difference: serio.i line 51205; libps2.i line 51649. This demonstrates that the original MiniC diagnostics are **not sufficient evidence of a Parser regression on valid inputs**.

Rerun the fail-closed 7-shard 3352 workflow with per-runner serial Kbuild materialization, and assert both GCC-valid corpus inputs BEFORE MiniC replay in shard f. Keep input preparation time separate from MiniC TU compile time. This one-shot workflow's self-path trigger must be removed after evidence is collected. The certified old 39-patch runtime/Image profile is unchanged. Do not infer full Image/QEMU certification from .i -> .s compile coverage.
