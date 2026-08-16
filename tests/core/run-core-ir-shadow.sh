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

cat >"$work_dir/pointer-parameter.i" <<'EOF'
int pointer_parameter(int *value) {
    return 7;
}
EOF

"$MINIC" -S "$work_dir/pointer-parameter.i" -o "$work_dir/pointer-parameter-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/pointer-parameter.i" \
    -o "$work_dir/pointer-parameter-shadow.s"
cmp "$work_dir/pointer-parameter-normal.s" "$work_dir/pointer-parameter-shadow.s"
MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/pointer-parameter.i" \
    -o "$work_dir/pointer-parameter-strict.s"
cmp "$work_dir/pointer-parameter-normal.s" "$work_dir/pointer-parameter-strict.s"

cat >"$work_dir/qualified-parameter.i" <<'EOF'
unsigned long qualified_parameter(const unsigned long value) {
    return value;
}
EOF

"$MINIC" -S "$work_dir/qualified-parameter.i" -o "$work_dir/qualified-parameter-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/qualified-parameter.i"     -o "$work_dir/qualified-parameter-shadow.s"
cmp "$work_dir/qualified-parameter-normal.s" "$work_dir/qualified-parameter-shadow.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/qualified-parameter.i"     -o "$work_dir/qualified-parameter-strict.s" 2>"$work_dir/qualified-parameter-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted a qualified parameter" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'qualified_parameter'"     "$work_dir/qualified-parameter-strict.err" >/dev/null

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

cat >"$work_dir/if-return.i" <<'EOF'
int select_return(int condition) {
    if (condition) {
        return 1;
    } else {
        return 2;
    }
}
EOF
check_strict_case if-return

cat >"$work_dir/if-assign-return.i" <<'EOF'
int select_assigned_return(int condition) {
    int value;
    if (condition) {
        value = 1;
        return value;
    } else {
        value = 2;
        return value;
    }
}
EOF
check_strict_case if-assign-return

cat >"$work_dir/if-empty-merge.i" <<'EOF'
int pass_through(int condition) {
    if (condition) {
    }
    return 7;
}
EOF
check_strict_case if-empty-merge

cat >"$work_dir/if-merge.i" <<'EOF'
int select_value(int condition) {
    int value;
    if (condition) {
        value = 1;
    } else {
        value = 2;
    }
    return value;
}
EOF
check_strict_case if-merge

cat >"$work_dir/while-backedge.i" <<'EOF'
int clear_then_add(int value) {
    while (value) {
        value = 0;
    }
    return value + 7;
}
EOF
check_strict_case while-backedge

cat >"$work_dir/for-shape.i" <<'EOF'
int for_shape(int value) {
    for (; value;) {
        value = 0;
    }
    return value;
}
EOF

"$MINIC" -S "$work_dir/for-shape.i" -o "$work_dir/for-shape-normal.s"
MINIC_CORE_IR=shadow "$MINIC" -S "$work_dir/for-shape.i" -o "$work_dir/for-shape-shadow.s"
cmp "$work_dir/for-shape-normal.s" "$work_dir/for-shape-shadow.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/for-shape.i" \
    -o "$work_dir/for-shape-strict.s" 2>"$work_dir/for-shape-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted canonical for-loop lowering" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'for_shape'" \
    "$work_dir/for-shape-strict.err" >/dev/null


cat >"$work_dir/direct-call-v0.i" <<'EOF'
int direct_callee(int value) {
    return value + 1;
}

int direct_caller(int value) {
    return direct_callee(value);
}
EOF
check_strict_case direct-call-v0

cat >"$work_dir/direct-void-call-v0.i" <<'EOF'
void direct_sink(int value) {
    return;
}

void direct_void_caller(int value) {
    direct_sink(value);
    return;
}
EOF
check_strict_case direct-void-call-v0

cat >"$work_dir/direct-pointer-result-statement-v0.i" <<'EOF'
int *external_pointer_identity(int *value);

void consume_pointer_call(int *value) {
    external_pointer_identity(value);
    return;
}
EOF
check_strict_case direct-pointer-result-statement-v0

cat >"$work_dir/indirect-call-unsupported.i" <<'EOF'
int indirect_caller(int (*callee)(int), int value) {
    return callee(value);
}
EOF
"$MINIC" -S "$work_dir/indirect-call-unsupported.i" \
    -o "$work_dir/indirect-call-unsupported-normal.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/indirect-call-unsupported.i" \
    -o "$work_dir/indirect-call-unsupported-strict.s" \
    2>"$work_dir/indirect-call-unsupported-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted an indirect call" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'indirect_caller'" \
    "$work_dir/indirect-call-unsupported-strict.err" >/dev/null

cat >"$work_dir/variadic-call-unsupported.i" <<'EOF'
int variadic_external(int first, ...);

int variadic_caller(int value) {
    return variadic_external(value, value);
}
EOF
"$MINIC" -S "$work_dir/variadic-call-unsupported.i" \
    -o "$work_dir/variadic-call-unsupported-normal.s"
if MINIC_CORE_IR=strict "$MINIC" -S "$work_dir/variadic-call-unsupported.i" \
    -o "$work_dir/variadic-call-unsupported-strict.s" \
    2>"$work_dir/variadic-call-unsupported-strict.err"; then
    echo "strict Core IR shadow unexpectedly accepted a variadic call" >&2
    exit 1
fi
grep -F "Core IR shadow does not yet support function 'variadic_caller'" \
    "$work_dir/variadic-call-unsupported-strict.err" >/dev/null

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
