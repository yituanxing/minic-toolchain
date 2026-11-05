#!/usr/bin/env bash
set -Eeuo pipefail

# No Linux download is needed: synthetic seven-shard result manifests exercise
# the exact same strict/diagnostic acceptance aggregator used in Actions.
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 - "$work" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
total = 2763
for shard in range(7):
    rows = [{"index": idx, "owner": f"drivers/gate_{idx}.o",
             "status": "ASSEMBLED_UNVERIFIED", "first_blocker": "-"}
            for idx in range(shard, total, 7)]
    doc = {"schema": "minias-kbuild-shard-result-v1", "target": "x86_64",
           "shard": shard, "manifest_sha256": "pinned-source",
           "config_sha256": "pinned-config", "full_kbuild": True,
           "expected_total": total, "records": rows,
           "mnemonics": {}, "directives": {}}
    p = root / "shards" / f"s{shard}" / "results.json"
    p.parent.mkdir(parents=True)
    p.write_text(json.dumps(doc))
PY

runner=(python3 "$repo/tools/ci/minias-linux-kbuild-seven-v1.py" aggregate
        --targets x86_64 --results "$work/shards")
"${runner[@]}" --strict --dest "$work/pass.json" >"$work/pass.log"
grep -q 'MINIAS_LINUX_S_STRICT_GATE=PASS' "$work/pass.log"
python3 - "$work" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
assert json.loads((root / "pass.json").read_text())["targets"]["x86_64"]["strict_candidate_pass"]
p = root / "shards" / "s0" / "results.json"
doc = json.loads(p.read_text())
doc["records"][0]["status"] = "CANDIDATE_FAIL"
p.write_text(json.dumps(doc))
PY
"${runner[@]}" --dest "$work/discovery.json" >"$work/discovery.log"
grep -q 'candidate_fail=1' "$work/discovery.log"
if "${runner[@]}" --strict --dest "$work/fail.json" >"$work/fail.log" 2>&1; then
    echo 'strict gate accepted CANDIDATE_FAIL' >&2
    exit 1
fi
test -s "$work/fail.json"   # A failing gate still publishes diagnostics.
grep -q 'strict Linux Kbuild candidate gate failed' "$work/fail.log"
python3 - "$work" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
p = root / "shards" / "s0" / "results.json"
doc = json.loads(p.read_text())
doc["records"][0]["status"] = "ASSEMBLED_UNVERIFIED"
doc["full_kbuild"] = False
p.write_text(json.dumps(doc))
PY
if "${runner[@]}" --strict --dest "$work/incomplete.json" >"$work/incomplete.log" 2>&1; then
    echo 'strict gate accepted incomplete Kbuild provenance' >&2
    exit 1
fi
grep -q 'mixed Kbuild/config/corpus identity' "$work/incomplete.log"
echo 'MINIAS_LINUX_STRICT_ACCEPTANCE=PASS complete=2763 candidate_fail=reject incomplete=reject discovery=preserved'
