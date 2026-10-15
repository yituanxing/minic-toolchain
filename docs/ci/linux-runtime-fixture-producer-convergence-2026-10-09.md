# Runtime frozen and compact fixture producer convergence — 2026-10-09

Two distinct, explicitly invoked fixture *producer* entrypoints now live in one maintained workflow `.github/workflows/linux-runtime-fixture-producers-v1.yml`. The two **independent job identities and outputs remain separate**, not renamed or re-keyed.

| Historical source workflow | Immutable source Git blob | Maintained job | Manual mode | Original push opt-in |
| --- | --- | --- | --- | --- |
| `linux-runtime-frozen-cert-v1.yml` | `00508c60c5f17ad0e4433f4b369c0b5eaef9de7a` | `frozen-cert` | `frozen` (default) | `[linux-runtime-frozen-cert-v1]` |
| `linux-runtime-link-fixture-cert-v1.yml` | `dbdf655f18123a786b4f8fadeb7328b87dd2a954` | `compact-cert` | `compact` | `[linux-runtime-link-fixture-cert-v1]` |

The original byte-identical YAMLs are archived under `.github/workflows-disabled/`. Each original job's entire execution body, Linux/GCC oracles, certified cache-key/path validation, action-version, shell steps, timeouts and uploads are unchanged. The old workflow-scoped `concurrency` is moved to matching independent *job-scoped* groups to prevent one producer mode cancelling the other. Pushing an explicit original tag triggers only its corresponding job; manual dispatch selects one mode.

**Do not infer dependencies:** a compact linked fixture may consume frozen input, but there is no new implicit `needs` and manual `compact` will fail on a missing certified upstream cache exactly as before. Run frozen then compact explicitly when rebuilding a producer chain. The two cache keys are unchanged; no expired artifact or missing certificate is silently substituted.

M0 `tools/ci/check_linux_runtime_fixture_producers_convergence_v1.py` enforces both archival Git SHA IDs, complete executable producer body equality, old opt-in tags and unique-mode manual routing. It is a **T0 structural proof, not a new T3 full linked-Image or T4 QEMU runtime certificate**. A real producer/replay must be obtained separately before claiming downstream artifact provenance.

This migration reduces active workflows from **36→35** on Runtime and **37→36** on Performance, while keeping the canonical full Image, GCC baseline and QEMU frontier workflows unchanged.
