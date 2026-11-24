Historical CI evidence is stored in Git, not in the current worktree.

The complete C59 archive (321 YAML files) is preserved at:
https://github.com/yituanxing/minic-toolchain/tree/de72e2982e9576ec36a8110a371287300ed29937/.github/workflows-disabled

The final 68 definitions required by the legacy integrity guards are pinned at:
https://github.com/yituanxing/minic-toolchain/tree/cd8786f4341c9de1049fe839fe37392b01faaa4a/.github/workflows-disabled

The M0 gate restores their exact Git blobs only while running historical
integrity checks; it removes them afterwards. For a local legacy guard use
  python3 tools/ci/historical_workflows.py run -- python3 tools/ci/check_minipp_frozen_convergence_v1.py
The pinned commit must be available in the local Git history. No archived
workflow is executable by GitHub Actions. Historical PASS is not current PASS.
