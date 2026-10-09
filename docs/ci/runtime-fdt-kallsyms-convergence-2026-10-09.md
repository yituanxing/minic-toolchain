# Linux FDT and Generated Kallsyms Diagnostic Convergence — 2026-10-09

Two opt-in canonical workflow entrypoints now share `.github/workflows/linux-runtime-fdt-isolation-v1.yml`. Five independently reported jobs remain: `fdt-isolation`, `fdt-ro-codegen`, `generated-kallsyms-first-die`, `kallsyms-first-fault-abc`, `kallsyms-object-diagnose`.

Archives preserving original Git blob SHA:
- `linux-runtime-fdt-isolation-v1.yml`: `83feffda9150072a849f677c58856f77975968cc`.
- `linux-runtime-generated-kallsyms-first-die-v0.yml`: `bd8f74957937f1bfbe9adcd30c54042fb6bdf38c`.

Both source workflows were limited to Runtime branch pushes for changes to their YAML or `tools/ci/linux-early-runtime-link-v2.sh`; their named jobs additionally required explicit historical commit tags. The merged workflow retains that shared link-script trigger and all five independent tag selectors. Manual modes: `isolation` (default), `codegen`, `generated`, `three-way`, `object`, `all`.

All executable job bodies, immutable cache keys, QEMU/GCC/MiniC assertions, timeouts and upload evidence remain unchanged. The only job property added is an independent job-scoped cancellation group. Previously each source workflow had its own workflow-level cancellation. Job-level cancellation includes the literal job ID so different probes cannot cancel one another when dispatched simultaneously.

`tools/ci/check_linux_runtime_fdt_kallsyms_union_v1.py` verifies both original Git blobs and byte-identical executable bodies (excluding only moved concurrency), modes, tags, and trigger. Existing M0 historical six specialized job body and archived source checks remain active, with the generated-kallsyms canonical path now routed through the combined owner.

This is structural T0 proof only. No new boot PASS or clean Kallsyms/first-die result is claimed. The source fixture and strict Image producer/consumer chain remain unchanged.

Expected active workflows: Runtime 32→31, Performance 33→32, 35 unique active workflow names.
