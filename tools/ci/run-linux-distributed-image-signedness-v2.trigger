# Explicit one-time GitHub Actions trigger for Linux Distributed Full Image Signedness V2.
# Evidence: Runtime 39+ICE + nested-constant-p V1/V2 passed all five real Kbuild blockers
# in run 37786873174, and 3352/3352 original distributed reuse preflight passed.
# This triggers seven fresh producer shards (new MiniC minic-cc compiler identity),
# then a strict receiver Image/QEMU pipeline. Old run 37767282927 artifacts are not
# permitted to masquerade as V2 compiler outputs.
triggered_after=37786873174
target_branch=agent/linux-expanded-kbuild-v0
