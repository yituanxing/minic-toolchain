#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC must point to the compiler binary}"

work_dir="${BUILD_DIR:-build/core-ir-shadow}/pipeline-shadow"
rm -rf "$work_dir"
mkdir -p "$work_dir"

cat >"$work_dir/supported.i" <<'EOF'
int main(void) {
    return 1 + 2;
}
EOF

"$MINIC" -S "$work_dir/supported.i" -o "$work_dir/supported-normal.s"
MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/supported.i" -o "$work_dir/supported-shadow.s"
cmp "$work_dir/supported-normal.s" "$work_dir/supported-shadow.s"

cat >"$work_dir/unsupported.i" <<'EOF'
int main(void) {
    return 1 - 2;
}
EOF

"$MINIC" -S "$work_dir/unsupported.i" -o "$work_dir/unsupported-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/unsupported.i" -o "$work_dir/unsupported-shadow.s"
cmp "$work_dir/unsupported-normal.s" "$work_dir/unsupported-shadow.s"

if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/unsupported.i" \
    -o "$work_dir/unsupported-strict.s" 2>"$work_dir/unsupported-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted an unsupported function" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'main'" \
    "$work_dir/unsupported-strict.err" >/dev/null

if MINIC_CORE_IR=invalid "$MINIC" -S "$work_dir/supported.i" \
    -o "$work_dir/invalid-mode.s" 2>"$work_dir/invalid-mode.err"; then
    echo "invalid Core IR shadow mode unexpectedly succeeded" >&2
    exit 1
fi
grep -F "MINIC_CORE_IR must be unset, 'shadow', or 'strict'" \
    "$work_dir/invalid-mode.err" >/dev/null
