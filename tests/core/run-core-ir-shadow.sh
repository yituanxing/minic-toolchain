#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC must point to the compiler binary}"

work_dir="${BUILD_DIR:-build/core-ir-shadow}/pipeline-shadow"
rm -rf "$work_dir"
mkdir -p "$work_dir"

check_strict_case() {
    local name="$1"

    "$MINIC" -S "$work_dir/$name.i" -o "$work_dir/$name-normal.s"
    MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/$name.i" -o "$work_dir/$name-shadow.s"
    cmp "$work_dir/$name-normal.s" "$work_dir/$name-shadow.s"
}

cat >"$work_dir/supported.i" <<'EOF'
int main(void) {
    return 1 + 2;
}
EOF
check_strict_case supported

cat >"$work_dir/local-object.i" <<'EOF'
int main(void) {
    int value = 1;
    return value;
}
EOF
check_strict_case local-object

cat >"$work_dir/volatile-object.i" <<'EOF'
int main(void) {
    volatile int value = 1;
    return value;
}
EOF
check_strict_case volatile-object

cat >"$work_dir/parameter.i" <<'EOF'
int add_one(int value) {
    return value + 1;
}
EOF
check_strict_case parameter

cat >"$work_dir/mixed-add.i" <<'EOF'
int add_mixed(signed char value) {
    return value + 1;
}
EOF
check_strict_case mixed-add

cat >"$work_dir/implicit-conversion.i" <<'EOF'
int widen(signed char value) {
    return value;
}

signed char narrow(void) {
    return 257;
}
EOF
check_strict_case implicit-conversion

cat >"$work_dir/explicit-conversion.i" <<'EOF'
int truncate(unsigned long value) {
    return (int)value;
}
EOF
check_strict_case explicit-conversion

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
