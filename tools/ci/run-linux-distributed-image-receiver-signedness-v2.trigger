# Explicit one-shot receiver-only V2 certification trigger.
# Seven immutable corrected-MiniC shard bundles from 37787452745 are reused.
# Previous full receiver produced real vmlinux/Image via make rc=0 in 117s;
# strict zero-recompile SHA check failed because 36 planned .o were regenerated
# after GNU Kbuild re-created SELinux/ASN1/vDSO generated dependencies.
# New receiver validates each changed .o/.cmd against exact manifest and
# successful local MiniC trace, with maximum 60 recompiles. Never waives checks.
source_run=37787452745
corrected_minic_sha=e41c4becdfb614ea736621ee0a449522a18a3ad2309258e9eeeff7bce317bd40
strict-replay-v3: replay pinned 3352 MiniC object shards after generated-dependency Kbuild warmup; require 6704 exact hashes and zero final producer recompiles
