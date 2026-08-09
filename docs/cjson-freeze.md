# cJSON clean-head freeze

The cJSON milestone is considered frozen only when the committed compiler source passes the normal compiler gate and the upstream cJSON differential without applying discovery patches first.

Validation uses unchanged upstream cJSON source and its upstream test program. MiniC and GCC builds run as RV64 Linux binaries under QEMU, with exit status and output compared exactly.

The discovery patches have now been materialized into the compiler source. Subsequent CI runs are the acceptance evidence for the clean baseline.
