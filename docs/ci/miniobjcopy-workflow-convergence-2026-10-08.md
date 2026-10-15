# MiniObjcopy integration workflow convergence — 2026-10-08

Before: three active workflow files, each with a separate contract. After:
one active canonical `.github/workflows/miniobjcopy-strip-regressions-v1.yml`
with three separate jobs and an explicit `workflow_dispatch.inputs.mode` selector:
`regressions` (default), `linux-tool`, `linux-image`, `all`.

No MiniC/MiniLD/MiniObjcopy source, test script, toolchain flags, job body
steps or shell commands were removed. The original regressions job retains
its push-triggered commit-message gate and is the only mode run on push.
The two old Linux jobs are manual-only with independent conditions;
`permissions.actions: read` is retained for the frozen artifact oracle.
Their original steps and shell bodies were moved intact (only top-level
job-level mode guards were added).

Archived unchanged:
- `.github/workflows-disabled/miniobjcopy-linux-tool-gate.yml`: original
  Git blob `60ee9e904a535bc599339def2efeed3f9b6c8c72`.
- `.github/workflows-disabled/miniobjcopy-linux-image-gate.yml`: original
  Git blob `69d74b896da76e99364c31291b98fbc9067905b1`.

The image oracle continues to hard-pin run `33623125809` and expects the
`static-linux-final-frontier` historical artifact. That artifact may already
be expired. This is historical test preservation, *not* a newly certified
runnable current-head Image contract. A new verified artifact producer must
be identified before that mode can become a current hard gate. The
`linux-tool` mode is an independent, expensive full Linux build and remains
manual only; no QEMU success is inferred.

Validation: structural M0 gate plus YAML parser, exact archive SHA checks;
then run only focused regressions on the consolidated file when needed.
No expensive Linux build is launched by this cleanup.
