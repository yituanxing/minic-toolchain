# Expanded Linux PI / P1 focused workflow convergence — 2026-10-09

## Entry-point replacement

`linux-expanded-pi-runtime-v0.yml` and `linux-expanded-runtime-p1-v0.yml` are now one canonical `.github/workflows/linux-expanded-pi-p1-runtime-v1.yml`.

| Historical workflow | Exact archived Git blob SHA | Job and manual mode | Push tag |
| --- | --- | --- | --- |
| `linux-expanded-pi-runtime-v0.yml` | `40b22c2ff0bda380df99ebbab99cea55f424aaa8` | `pi-runtime` / `pi` | `[linux-expanded-pi-runtime]` |
| `linux-expanded-runtime-p1-v0.yml` | `93677aa0c74dc00cfba0544535fca5b841dd2b51` | `p1` / `p1` | `[linux-expanded-runtime-p1]` |

Both original workflow bytes are archived exactly under `.github/workflows-disabled/` on both development branches. M0 checker `tools/ci/check_linux_expanded_pi_p1_convergence_v1.py` verifies both archive blobs, exact preserved executable job bodies, distinct original push tags and manual `p1` (default), `pi`, `all` modes.

Only PI originally had workflow-level `cancel-in-progress: true` and a concurrency group. That group is now job-scoped for PI, keeping the original group identity and cancellation behavior without cancelling unrelated P1 tests. P1 remains independent and has no new concurrency policy.

No compiler source, profile script, Linux cached fixture, QEMU test command, timeout, or artifact identity changed. T0 structural M0 passing is necessary but insufficient for current-head T4 Linux boot/diagnosis PASS; real PI/P1 diagnostic modes still require their own evidence. These are diagnostic entrypoints, not replacements for certified GCC baseline, link-fixture producer or final seven-shard Image.

Post-migration target counts: Runtime **41→40**, Performance **42→41**, distinct active workflows **45→44**.
