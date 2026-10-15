# Kallsyms refresh first-die retirement contract

The historical `.github/workflows/linux-runtime-kallsyms-refresh-first-die-v0.yml` workflow is an intermediate diagnostic and is retired from the active Actions set.

Its responsibilities are now covered by three active workflows:

- `linux-runtime-kallsyms-object-diagnose-v0.yml` retains cached/GCC/current-MiniC object provenance, relocation and code-generation evidence.
- `linux-runtime-first-die-context-v0.yml` runs the cached/GCC/current-MiniC three-way first-fault A/B/C comparison and classifies stale cached kallsyms behavior.
- `linux-runtime-generated-kallsyms-first-die-v0.yml` refreshes the current MiniC kallsyms owner, relinks with generated kallsyms data, verifies generated-kallsyms consistency and captures the resulting primary first-die context.

The retired YAML is preserved byte-for-byte under `.github/workflows-disabled/`.
