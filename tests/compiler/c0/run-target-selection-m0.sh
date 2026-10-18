#!/bin/sh
# M0 smoke: explicit target identity reaches Parser/Core without changing the
# frozen default RV64 compiler contract. No GNU cross tools are required.
set -eu

: "${MINIC_CC:?set MINIC_CC to the native minic-cc binary}"
temporary_dir=$(mktemp -d)
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM

cat > "$temporary_dir/basic.i" <<'EOF'
int main(void) { return 42; }
EOF

"$MINIC_CC" -S "$temporary_dir/basic.i" -o "$temporary_dir/default.s"
"$MINIC_CC" --target=riscv64-linux -S "$temporary_dir/basic.i" -o "$temporary_dir/explicit.s"
cmp "$temporary_dir/default.s" "$temporary_dir/explicit.s"

"$MINIC_CC" --target=x86_64-linux -S "$temporary_dir/basic.i" -o "$temporary_dir/x86.s"
grep -F 'main:' "$temporary_dir/x86.s" >/dev/null
grep -F 'movabsq' "$temporary_dir/x86.s" >/dev/null

# The x86 model must not silently reuse RV64's unsigned plain-char semantics.
# The selected target is applied during parsing, before the missing backend.
cat > "$temporary_dir/signed-char.i" <<'EOF'
_Static_assert((char)-1 < 0, "x86-64 plain char must be signed");
int main(void) { return 0; }
EOF
if "$MINIC_CC" --target=riscv64-linux -S "$temporary_dir/signed-char.i" \
        -o "$temporary_dir/wrong.s" 2>"$temporary_dir/rv-char.err"; then
    echo "FAIL: RV64 must reject the signed-char static assertion" >&2
    exit 1
fi
test ! -e "$temporary_dir/wrong.s"
"$MINIC_CC" --target=x86_64-linux -S "$temporary_dir/signed-char.i" -o "$temporary_dir/x86-char.s"
grep -F 'main:' "$temporary_dir/x86-char.s" >/dev/null

# C declaration-specifiers can interleave qualifiers and integer keywords.
cat > "$temporary_dir/mixed-qualifiers.i" <<'EOF'
int main(void) {
    unsigned const char a = 20;
    long volatile int b = 22;
    unsigned char const *q = &a;
    return (int)*q + (int)b;
}
EOF
"$MINIC_CC" --target=riscv64-linux -S "$temporary_dir/mixed-qualifiers.i" -o "$temporary_dir/rv-mixed.s"
"$MINIC_CC" --target=x86_64-linux -S "$temporary_dir/mixed-qualifiers.i" -o "$temporary_dir/x86-mixed.s"
test -s "$temporary_dir/rv-mixed.s"
test -s "$temporary_dir/x86-mixed.s"

if "$MINIC_CC" --target=unknown-target -S "$temporary_dir/basic.i" \
        -o "$temporary_dir/unknown.s" 2>"$temporary_dir/unknown.err"; then
    echo "FAIL: unknown target unexpectedly accepted" >&2
    exit 1
fi
grep -F 'unsupported target' "$temporary_dir/unknown.err" >/dev/null
test ! -e "$temporary_dir/unknown.s"

echo "PASS: target selection M0; RV64 default parity; x86 signed-char semantics and codegen"
