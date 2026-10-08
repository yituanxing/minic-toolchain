# Active workflow trigger liveness and immutable artifact audit — 2026-10-09

## Trigger liveness

Repository has exactly four branches (`main`, Runtime, performance and passive
archive). Five old workflow files formerly listened **only** on deleted
development branches. They have been updated (identically on both dev
branches) to use the two current dev branch refs, without touching any
existing executable job commands, test expectations or build scripts.

| Workflow | Deleted-only old branch | Current execution policy |
| --- | --- | --- |
| `linux-runtime-gcc-baseline.yml` | `refactor/declaration-sema-v1` | current branch, path-scoped, opt-in tag `[linux-runtime-gcc]` or manual dispatch |
| `linux-vdso-focused.yml` | `refactor/declaration-sema-v1` | current branch, path-scoped, opt-in tag `[linux-vdso-focused]` or manual dispatch |
| `linux-efistub-diff-v0.yml` | `agent/linux-link-correctness-v0` | current branch, path-scoped, opt-in tag `[linux-efistub-diff]` or manual dispatch |
| `minic-driver-v0.yml` | `toolchain/minic-driver-v0` | current branch, path-scoped, opt-in tag `[minic-driver-v0]` or manual dispatch |
| `minipp-a0.yml` | `toolchain/minipp-*` | current branch, existing narrow preprocessor path filter; cheap MiniPP A0 micro gate |

Only these five workflows are changed. On current refs the GCC baseline,
vDSO, EFI stub and MiniC driver jobs are tag-controlled to prevent an
untagged push from launching a costly Linux or musl run. MiniPP A0 remains
automatic for actual preprocessor-related source/Makefile changes; it
already has a narrow path list and an under-10-minutes job.
All old job steps remain unchanged.

Other current workflows with a **live runtime branch plus an unused old
branch** do not need urgent semantic changes. Historical manually
dispatched-only workflows without current-branch push triggers still
require scrutiny before calling them current runnable fixtures.

## Exact remote artifact audit (REST API queried 2026-10-09)

- Frozen Linux corpus source run **33186855250**: success and **7**
  unexpired frozen corpus artifacts; expiry **2026-11-26 15:47 UTC**.
- MiniAS sidecar run **33237304382**: success and **1** unexpired
  `minias-linux-sidecars-v1`; expiry **2026-11-27 05:56 UTC**.
- MiniAS native C149 run **33248985705**: success and **1**
  unexpired `minias-ground-truth-c149-corpus`; expiry
  **2026-11-27 10:57 UTC**.
- Historical MiniObjcopy `linux-image` source run **33623125809**:
  workflow run *success*, but currently **zero** downloadable artifacts.
  The canonical combined MiniObjcopy workflow keeps this exact frozen
  comparison job as archived evidence; it **cannot** currently pass its
  `download-artifact` step. Do not replace the missing original with
  an unrelated recent run or weaken its SHA/identity checks.
- None of these availability results proves that every current workflow
  has a valid cache in its own *branch-scoped* Actions cache. An artifact
  run existing does not guarantee cache restoration or final test success.

## Current hard gates still outstanding

- The consolidated seven-shard self-producing full Image workflow has
  M0 structure validation but not a new all-shard current-HEAD full
  relink certificate.
- MiniAS complete 3536 exact/semantic tests have not been rerun after
  workflow cleanup; fixed corpus/sidecar artifacts still exist today.
- Historical MiniObjcopy Image oracle requires a trustworthy current
  producer and independently checked byte-identical fixture.
- Linux QEMU's known full-MiniC Image evidence remains OpenSBI-only
  timeout, not a boot PASS.

Keep old source run identities intact and do not reactivate arbitrary
noncanonical workflow versions just to make an artifact downloadable.

## Structural proof on current Runtime head

- [M0 run 37861656075](https://github.com/yituanxing/minic-toolchain/actions/runs/37861656075)
  **SUCCESS**: `M0_LIVE_TRIGGER_YAML=PASS`,
  `M0_LIVE_TRIGGERS=PASS current_dev_branches=2 fixed_workflows=5`,
  plus preserved Core 3352 and focused-five archive SHA/job body checks.
- The same repaired five workflow Git blob SHAs are mirrored in the
  performance branch; no compiler/runtime/test implementation changed.
- Artifact expiration dates remain external and time-sensitive.
  This report describes the 2026-10-09 check, not an everlasting
  artifact guarantee.
