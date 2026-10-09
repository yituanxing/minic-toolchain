#!/usr/bin/env python3
"""Preserve original Runtime Owner job byte-for-byte with a new matrix router."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parents[2]
YAML=ROOT/".github/workflows/linux-runtime-owner-focused-v1.yml"
OLD_SHA="c9b8fff60a2a805ceff68e1d028da69f7485dceb"
NEW_SHA="f751fe83c49947302201e9cd09d313a865735372"
SELECTOR=ROOT/"tools/ci/select_runtime_owner_modes_v1.py"

OLD_PATH='      - "tools/ci/runtime-build-policy-trigger.txt"\n'
NEW_PATH=OLD_PATH+'      - "tools/ci/select_runtime_owner_modes_v1.py"\n'
OLD_DISPATCH="  workflow_dispatch:\n"
NEW_DISPATCH="""  workflow_dispatch:
    inputs:
      mode:
        description: "Select one owner diagnosis or all four"
        required: true
        type: choice
        default: all
        options:
          - all
          - timekeeping
          - vsyscall
          - notifier
          - build-policy
"""
ROUTE_BLOCK=r"""jobs:
  route:
    runs-on: ubuntu-24.04
    timeout-minutes: 3
    outputs:
      modes: ${{ steps.select.outputs.modes }}
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ github.sha }}
          fetch-depth: 0
      - name: Resolve changed owner trigger paths and manual mode
        id: select
        shell: bash
        env:
          EVENT_KIND: ${{ github.event_name }}
          BEFORE_SHA: ${{ github.event.before }}
          AFTER_SHA: ${{ github.sha }}
          REQUESTED_MODE: ${{ inputs.mode || 'all' }}
        run: |
          set -Eeuo pipefail
          python3 tools/ci/select_runtime_owner_modes_v1.py \
            --event "$EVENT_KIND" --before "$BEFORE_SHA" --after "$AFTER_SHA" \
            --mode "$REQUESTED_MODE" >>"$GITHUB_OUTPUT"

  owner:
    needs: route
    strategy:"""
ORIGINAL_OWNER="jobs:\n  owner:\n    strategy:"
OLD_MATRIX="        mode: [timekeeping, vsyscall, notifier, build-policy]"
NEW_MATRIX="        mode: ${{ fromJSON(needs.route.outputs.modes) }}"

def blob_sha(text):
    data=text.encode()
    return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def restore_owner_text(text):
    if blob_sha(text)!=NEW_SHA:
        raise AssertionError("Runtime Owner routing job/conditions changed without independent review")
    for expected in (NEW_PATH,NEW_DISPATCH,ROUTE_BLOCK,NEW_MATRIX):
        if text.count(expected)!=1:
            raise AssertionError(f"expected unique Runtime owner routing text missing: {expected[:55]}")
    text=text.replace(NEW_PATH,OLD_PATH).replace(NEW_DISPATCH,OLD_DISPATCH)
    text=text.replace(ROUTE_BLOCK,ORIGINAL_OWNER).replace(NEW_MATRIX,OLD_MATRIX)
    if blob_sha(text)!=OLD_SHA:
        raise AssertionError("Runtime Owner test body or original push policy changed")
    return text

def main():
    restore_owner_text(YAML.read_text())
    if not SELECTOR.exists():
        raise AssertionError("Runtime Owner matrix selector is missing")
    print("M0_RUNTIME_OWNER_ROUTER=PASS exact_prior_workflow_sha=1 route_and_manual_modes=5 owner_steps_unchanged=1")

if __name__=="__main__":
    main()
