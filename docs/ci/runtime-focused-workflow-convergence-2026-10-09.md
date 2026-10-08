# Linux Runtime focused-probe workflow convergence — 2026-10-09

This is workflow structural maintenance only. Nine original independently
implemented focused diagnostics have been consolidated to three active
workflow entrances **with all nine original job bodies still present**.
The six retired YAML files were moved to .github/workflows-disabled/ by
their *exact original Git blob SHA*, and no runtime/compiler code, cache
identity, status checker, or QEMU harness was edited.

Old diagnostic workflows had branch-path/self triggers. The consolidated
workflows now deliberately gate expensive runs behind either an explicit
workflow_dispatch *mode* (choice includes 'all') or a commit-message
tag on an allowed push path. This narrows incidental automatic triggers
but preserves the ability to execute every historical check. The original
job's steps, script bodies, cache keys, timeouts, artifact names and shell
assertions are retained unchanged; only job-level `if` selectors were
added. GitHub workflow_dispatch on non-default refs may require default
branch registration; tagged push remains an intentional alternative.

| Canonical maintained owner | Retired exact workflows / SHA | Explicit commit tags |
|---|---|---|
| `linux-runtime-spinlock-context-v0.yml` | `linux-runtime-spinlock-codegen-v0.yml` `dbd4e9cf8be3aca8e0b23359a8daf4283649bd9f`; `linux-runtime-spinlock-first-context-v0.yml` `83d77ea7dfaff4eb204b902363df42c428063292`; `linux-runtime-spinlock-stack-v0.yml` `a9e55e5f74309e090b78fe3dc4d141502d5866c2` | `[linux-spinlock-context]`, `[linux-spinlock-codegen]`, `[linux-spinlock-first-context]`, `[linux-spinlock-stack]` |
| `linux-runtime-fdt-isolation-v1.yml` | `linux-runtime-fdt-ro-codegen-v0.yml` `bbac9494c79603df4146fb08118ee1007a6b66c9` | `[linux-fdt-isolation]`, `[linux-fdt-codegen]` |
| `linux-runtime-generated-kallsyms-first-die-v0.yml` | `linux-runtime-first-die-context-v0.yml` `90e6d9e2244bb4a602321617c661a8a33c5faa65`; `linux-runtime-kallsyms-object-diagnose-v0.yml` `b0321f094495926553191f8d5ba85566ce3a6a62` | `[linux-kallsyms-generated]`, `[linux-kallsyms-three-way]`, `[linux-kallsyms-object]` |

Jobs preserved:
- `linux-runtime-spinlock-context-v0.yml`: `spinlock-context=context`; `spinlock-codegen=codegen`; `spinlock-first-context=first-context`; `spinlock-stack=stack`; manual modes `context`, `codegen`, `first-context`, `stack`, `all`.
- `linux-runtime-fdt-isolation-v1.yml`: `fdt-isolation=isolation`; `fdt-ro-codegen=codegen`; manual modes `isolation`, `codegen`, `all`.
- `linux-runtime-generated-kallsyms-first-die-v0.yml`: `generated-kallsyms-first-die=generated`; `kallsyms-first-fault-abc=three-way`; `kallsyms-object-diagnose=object`; manual modes `generated`, `three-way`, `object`, `all`.

Source branch immediately before this convergence: `005c7f47063c0815d0a4bb81eb2acbf6e852ff87`.
The same original Git blob SHA for all nine input workflows was verified
on `agent/linux-perf-boolean-domain-v1` before the change, so the
consolidated exact YAML blobs can be shared across both dev branches.

**Not combined**: `linux-runtime-fault-context-v1.yml` uses a distinct
compact link-fixture cache and fault-watch contract; the generated kallsyms
full fixture / FDT and spinlock probes are different. Similarly GCC
baseline, frozen cert, runtime first-fault watch, owner-focused, and full
Image validation remain independent and are not superseded by this cleanup.

**Proven evidence limits:** M0 YAML parsing/structure/archive-identity
checks are the acceptance gate for this structural merge. This change
does not prove any of the heavyweight QEMU probes pass on current HEAD.
The only known complete MiniC Image QEMU evidence remains OpenSBI-only
after 90s timeout; Linux boot is not certified.
