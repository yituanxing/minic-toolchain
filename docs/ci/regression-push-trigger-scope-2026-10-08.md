# Trigger scope for retained canonical smoke regressions (2026-10-08)

Retained owners: `minic-rv64-focused-regressions-v1.yml`, `miniar-regressions-v1.yml`, `minild-regressions-v1.yml`.

All three previously ran unconditionally on **every** push to the runtime branch, including commits that only archived historical YAMLs or updated CI documentation. Their automatic `push` events now require source, compiler tooling, executable tests, build scripts, shared custom actions, or that workflow's own YAML to change. All three keep `workflow_dispatch` for explicit manual certification.

Allowed push changed paths (broad by design, fail-open toward real code impact): `src/**`, `include/**`, `compiler/**`, `preprocessor/**`, `assembler/**`, `archiver/**`, `linker/**`, `tools/**`, `tests/**`, `scripts/**`, `Makefile`, `.github/actions/**`, plus the workflow's own `.github/workflows/<name>.yml`.

This retains automatic validation on production-code/test/build changes without creating unnecessary Runner jobs for unrelated workflow inventories, markdown-only notes, or other workflow retirements. Keep Linux Image/QEMU contracts unchanged and do not construe this CI-only edit as proof of candidate new-profile boot.
