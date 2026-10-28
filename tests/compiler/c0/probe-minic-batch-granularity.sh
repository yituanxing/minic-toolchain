#!/usr/bin/env bash
# The batch auditor must count effective code and evidence, not markdown,
# CI configuration or binary blobs. Validate both committed and worktree modes.
set -Eeuo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
tool="$root/tools/ci/minic-batch-granularity.py"
test -f "$tool"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git -C "$work" init -q
git -C "$work" config user.name 'MiniC Batch Test'
git -C "$work" config user.email 'minic-batch@example.invalid'
mkdir -p "$work/src" "$work/tests" "$work/docs" "$work/.github/workflows"
printf '%s\n' 'int old(void) { return 0; }' > "$work/src/main.c"
git -C "$work" add .
git -C "$work" commit -qm initial
base="$(git -C "$work" rev-parse HEAD)"

printf '%s\n' 'int changed(void) {' '    return 1;' '}' > "$work/src/main.c"
printf '%s\n' 'test a' 'test b' 'test c' 'test d' 'test e' > "$work/tests/case.sh"
printf '%s\n' 'doc a' 'doc b' 'doc c' 'doc d' 'doc e' \
    'doc f' 'doc g' 'doc h' 'doc i' 'doc j' > "$work/docs/report.md"
printf '%s\n' 'workflow a' 'workflow b' > "$work/.github/workflows/gate.yml"
git -C "$work" add .
git -C "$work" commit -qm feature
head="$(git -C "$work" rev-parse HEAD)"

(
    cd "$work"
    python3 "$tool" --base "$base" --head "$head" \
        --minimum 9 --maximum 9 --strict > "$work/report"
)
grep -Fq 'effective=9' "$work/report"
grep -Fq 'gross=21' "$work/report"
grep -Fq 'implementation: +3 -1 changed=4' "$work/report"
grep -Fq 'verification: +5 -0 changed=5' "$work/report"
grep -Fq 'workflow: +2 -0 changed=2' "$work/report"
grep -Fq 'documentation: +10 -0 changed=10' "$work/report"
grep -Fq 'result=WITHIN_TARGET' "$work/report"

if (cd "$work" && python3 "$tool" --base "$base" --head "$head" \
    --minimum 10 --maximum 30 --strict > "$work/rejected"); then
    echo 'MiniC grain check incorrectly passed an undersized batch' >&2
    exit 1
fi
grep -Fq 'result=BELOW_TARGET' "$work/rejected"
if (cd "$work" && python3 "$tool" --base "$base" --head "$head" \
    --minimum 1 --maximum 8 --strict > "$work/rejected"); then
    echo 'MiniC grain check incorrectly passed an oversized batch' >&2
    exit 1
fi
grep -Fq 'result=ABOVE_TARGET' "$work/rejected"

# NUL-delimited rename records have a different field layout from ordinary
# numstat entries. Count only the actual new line at its destination path;
# a moved file's old test contents are not newly written implementation.
git -C "$work" mv tests/case.sh src/renamed.sh
printf '%s\n' 'new source line' >> "$work/src/renamed.sh"
git -C "$work" add .
git -C "$work" commit -qm rename
rename_head="$(git -C "$work" rev-parse HEAD)"
(
    cd "$work"
    python3 "$tool" --base "$head" --head "$rename_head" \
        --minimum 1 --maximum 1 --strict > "$work/rename-report"
)
grep -Fq 'effective=1' "$work/rename-report"
grep -Fq 'implementation: +1 -0 changed=1' "$work/rename-report"
grep -Fq 'verification: +0 -0 changed=0' "$work/rename-report"

printf '%s\n' 'new test a' 'new test b' > "$work/tests/untracked.sh"
printf '\000\001\002' > "$work/tests/blob.bin"
printf '%s\n' 'doc only' > "$work/docs/untracked.md"
(
    cd "$work"
    python3 "$tool" --base HEAD --worktree --minimum 2 --maximum 2 \
        --strict > "$work/worktree-report"
)
grep -Fq 'effective=2' "$work/worktree-report"
grep -Fq 'gross=3' "$work/worktree-report"
grep -Fq 'result=WITHIN_TARGET' "$work/worktree-report"

echo 'MINIC_BATCH_GRANULARITY=PASS committed=9 worktree=2 excludes=docs,workflow,binary strict=both_bounds'
