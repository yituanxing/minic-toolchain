# Expanded Linux Entry Trace integrated into canonical PI/P1 owner — 2026-10-09

The existing canonical `linux-expanded-pi-p1-runtime-v1.yml` now owns three independent job identities: `pi-runtime`, `p1`, `trace`. The original manually selectable modes `pi`, `p1` (default), `all` remain; new mode `trace` selects only the historical Entry Trace oracle. The push tag `[linux-expanded-entry-trace]` remains independently opt-in.

**Cross-branch difference deliberately retained:** Original `linux-expanded-entry-trace-v0.yml` Git blob SHA:

- Runtime branch: `7b40efe922e288bb6dadab208f851849867d03b0`, uses current-profile-based linked fixture identity.
- Performance branch: `ff15c0643a93b8025fb32766ad94bb46123ae67e`, has a different historical linked-fixture restoration procedure.

Each branch therefore has its *own* combined YAML Git blob with the relevant original Entry Trace test body. Neither historical source was overwritten: both originals are now stored verbatim under `.github/workflows-disabled/linux-expanded-entry-trace-v0.yml` on their matching development branch. The updated `tools/ci/check_linux_expanded_pi_p1_convergence_v1.py` selects the original blob SHA by `GITHUB_REF_NAME`, verifies all three exact executable job bodies and all existing tags/modes/cancellation policies. It still verifies PI/P1 original archived blobs exactly.

The new `trace` job uses the prior `linux-expanded-entry-trace-${{ github.ref }}` cancel-in-progress group at **job scope**, preserving its group identity. PI keeps its distinct job-scoped cancellation group; P1 still has no special concurrency.

The canonical workflow's push branch is still only `agent/linux-expanded-kbuild-v0`, matching Entry Trace, PI and P1 original push eligibility. All remain manually dispatchable on either development branch. The change does not loosen fixture SHA/cache validation nor alter any MiniC/compiler tests.

**Structural M0 SUCCESS is not proof of a new current-HEAD boot or Runtime PASS.** Real mode-specific trace and PI/P1 QEMU runs require T4 proof separately.

After this consolidation, expected active definition counts: **37 Runtime / 38 Performance / 41 distinct**. No new top-level workflow was introduced.
