#!/usr/bin/env python3
"""Guard identical Linux fixture restore semantics across focused runtime owners.

Round-trip the changed YAML back to the pinned historical Git blob: no job
body/step/trigger change except an exact cache-restore -> composite invocation.
"""
from pathlib import Path
import hashlib, re
from check_runtime_owner_routing_v1 import restore_owner_text

ROOT = Path(__file__).resolve().parents[2]
ACTION = ROOT / ".github/actions/linux-runtime-restore-fixture/action.yml"
ACTION_SHA = "011a0e2162dd15c69336241afa498fd4c9631d6e"

SOURCE_KEY = "linux-source-6.6.143-dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932-v1"
FROZEN_KEY = "linux-runtime-frozen-v1-6.6.143-e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7"
SOURCE_PATH = "build/linux-expanded-kbuild-v0/linux-6.6.143"
FROZEN_PATH = "build/linux-runtime-frozen-v1"
SOURCE_ORIGINAL = (
    "      - name: Restore pinned Linux source\n"
    "        uses: actions/cache/restore@v4\n"
    "        with:\n"
    f"          path: {SOURCE_PATH}\n"
    f"          key: {SOURCE_KEY}\n"
    "          fail-on-cache-miss: true"
)
FROZEN_ORIGINAL = (
    "      - name: Restore certified frozen linker source subset\n"
    "        uses: actions/cache/restore@v4\n"
    "        with:\n"
    f"          path: {FROZEN_PATH}\n"
    f"          key: {FROZEN_KEY}\n"
    "          fail-on-cache-miss: true"
)
SOURCE_REPLACED = (
    "      - name: Restore pinned Linux source\n"
    "        uses: ./.github/actions/linux-runtime-restore-fixture\n"
    "        with:\n"
    "          fixture: source"
)
FROZEN_REPLACED = (
    "      - name: Restore certified frozen linker source subset\n"
    "        uses: ./.github/actions/linux-runtime-restore-fixture\n"
    "        with:\n"
    "          fixture: frozen"
)
WORKFLOWS = {
    "linux-runtime-mm-core-frontier-v0.yml": ("4c3d6fade380edab6349abf97a88390f92f24bcc", True),
    "linux-runtime-rcu-owner-v0.yml": ("a3041a6f9f03351c9785e08d6c379b024d714bc9", False),
    "linux-runtime-timer-focused-v0.yml": ("b24b8225264a1b25808ba5eca2ee0790002c2834", True),
    "linux-runtime-owner-focused-v1.yml": ("a0a39616cf5631bbcdf01500456df2aad9201554", True),
    "linux-runtime-riscv-init-codegen-v0.yml": ("6021f20acfc51df96d8e455af2e500e1f663f7e3", False),
}

def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

def check_action():
    data = ACTION.read_bytes()
    if git_blob(data) != ACTION_SHA:
        raise AssertionError("composite action changed; re-review exact cache behavior")
    text = data.decode()
    if text.count("uses: actions/cache/restore@v4") != 2:
        raise AssertionError("fixture action cache restoration step count changed")
    for mode, path, key in (("source", SOURCE_PATH, SOURCE_KEY), ("frozen", FROZEN_PATH, FROZEN_KEY)):
        selector = f"if: inputs.fixture == '{mode}'"
        part = text.split(selector, 1)
        if len(part) != 2:
            raise AssertionError(f"fixture mode missing: {mode}")
        step = part[1].split("\n    - name:", 1)[0]
        for expected in (f"path: {path}", f"key: {key}", "fail-on-cache-miss: true"):
            if expected not in step:
                raise AssertionError(f"{mode} lost immutable {expected}")
    if "if: inputs.fixture != 'source' && inputs.fixture != 'frozen'" not in text:
        raise AssertionError("unknown fixture would be silently accepted")

def main():
    check_action()
    replaced_source = replaced_frozen = 0
    for filename, (sha, uses_frozen) in WORKFLOWS.items():
        # Retired owners are immutable archives; their exact fixture-source
        # equivalence remains an active M0 obligation after 4->1 consolidation.
        # The canonical job-body guard independently compares archived bodies
        # to every new Runtime focused owner Job.
        folder = ".github/workflows" if filename == "linux-runtime-owner-focused-v1.yml" else ".github/workflows-disabled"
        path = ROOT / folder / filename
        text = path.read_text()
        if filename == "linux-runtime-owner-focused-v1.yml":
            text = restore_owner_text(text)
        if text.count(SOURCE_REPLACED) != 1:
            raise AssertionError(f"source fixture call missing or repeated: {filename}")
        if text.count(FROZEN_REPLACED) != int(uses_frozen):
            raise AssertionError(f"frozen fixture call count changed: {filename}")
        if "uses: actions/cache/restore@v4" not in text:
            raise AssertionError(f"unexpected disappearance of independent linked fixture: {filename}")
        before = text.replace(SOURCE_REPLACED, SOURCE_ORIGINAL).replace(FROZEN_REPLACED, FROZEN_ORIGINAL)
        if git_blob(before.encode()) != sha:
            raise AssertionError(f"historical workflow body/trigger changed: {filename}")
        replaced_source += 1
        replaced_frozen += int(uses_frozen)
    print(f"M0_RUNTIME_FIXTURE_REUSE=PASS owners={len(WORKFLOWS)} source_restores={replaced_source} frozen_restores={replaced_frozen} exact_original_workflows=5")

if __name__ == "__main__":
    main()
