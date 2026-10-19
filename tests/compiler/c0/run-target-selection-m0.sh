#!/bin/sh
# M0 smoke: explicit target identity reaches Parser/Core without changing the
# frozen default RV64 compiler contract. No GNU cross tools are required.
set -eu

: "${MINIC_CC:?set MINIC_CC to the native minic-cc binary}"
: "${HOST_CC:=cc}"
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

# Actual Lua 5.5.0 math.h pressure: glibc declares genuine _Float128
# functions. The target type is 16/16 IEEE binary128, not x86 long double.
# Declaration and type queries are supported; backend FP128 calls remain
# unsupported until actual use and an ABI-verified implementation.
cat > "$temporary_dir/float128-prototype.i" <<'EOF'
_Static_assert(sizeof(_Float128) == 16, "binary128 storage must be 16 bytes");
_Static_assert(_Alignof(_Float128) == 16, "binary128 alignment must be 16");
_Static_assert(!__builtin_types_compatible_p(_Float128, long double),
               "binary128 must not alias 80-bit long double");
extern int __fpclassifyf128(_Float128 x);
int main(void) { return 42; }
EOF
"$MINIC_CC" --target=x86_64-linux -S "$temporary_dir/float128-prototype.i" -o "$temporary_dir/float128-x86.s"
grep -F 'main:' "$temporary_dir/float128-x86.s" >/dev/null
if "$MINIC_CC" --target=riscv64-linux -S "$temporary_dir/float128-prototype.i" \
        -o "$temporary_dir/float128-rv.s" 2>"$temporary_dir/float128-rv.err"; then
    echo "FAIL: RV64 must not silently accept x86 binary128 type" >&2
    exit 1
fi
test ! -e "$temporary_dir/float128-rv.s"

# Real SQLite 3.54.1 glibc <stdlib.h> declares strtof32/strtof64.
# GCC confirms distinct C identities despite identical object layout;
# detect a wrong alias rather than quietly asserting ABI compatibility.
cat > "$temporary_dir/extended-float-prototypes.i" <<'EOF'
_Static_assert(sizeof(_Float32) == 4 && _Alignof(_Float32) == 4, "f32 layout");
_Static_assert(sizeof(_Float64) == 8 && _Alignof(_Float64) == 8, "f64 layout");
_Static_assert(sizeof(_Float32x) == 8 && _Alignof(_Float32x) == 8, "f32x layout");
_Static_assert(sizeof(_Float64x) == 16 && _Alignof(_Float64x) == 16, "f64x layout");
_Static_assert(!__builtin_types_compatible_p(_Float32, float), "f32 identity");
_Static_assert(!__builtin_types_compatible_p(_Float64, double), "f64 identity");
_Static_assert(!__builtin_types_compatible_p(_Float32x, double), "f32x identity");
_Static_assert(!__builtin_types_compatible_p(_Float64x, long double), "f64x identity");
extern _Float32 strtof32(const char *, char **);
extern _Float64 strtof64(const char *, char **);
extern _Float32x strtof32x(const char *, char **);
extern _Float64x strtof64x(const char *, char **);
int main(void) { return 42; }
EOF
"$HOST_CC" -std=gnu11 -c "$temporary_dir/extended-float-prototypes.i" \
    -o "$temporary_dir/gcc-extended-float.o"
"$MINIC_CC" --target=x86_64-linux -S \
    "$temporary_dir/extended-float-prototypes.i" \
    -o "$temporary_dir/x86-extended-float.s"
grep -F 'main:' "$temporary_dir/x86-extended-float.s" >/dev/null
if "$MINIC_CC" --target=riscv64-linux -S \
    "$temporary_dir/extended-float-prototypes.i" \
    -o "$temporary_dir/rv-extended-float.s" 2>"$temporary_dir/rv-extended-float.err"; then
    echo "FAIL: RV64 must not silently accept x86 glibc extended floating types" >&2
    exit 1
fi
test ! -e "$temporary_dir/rv-extended-float.s"
echo "PASS: x86 extended _Float32/_Float64/_Float32x/_Float64x distinct ABI layouts"

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

# Four-backend M0 must distinguish a registered but unfinished target
# from an unknown name, and MUST NOT emit x86 or RV64 assembly by accident.
# This is an intentional negative test, not an ABI/code-generation PASS.
for target in aarch64-linux loongarch64-linux; do
    if "$MINIC_CC" --target="$target" -S "$temporary_dir/basic.i" \
            -o "$temporary_dir/$target.s" 2>"$temporary_dir/$target.err"; then
        echo "FAIL: M0 $target emitted code without its target backend" >&2
        exit 1
    fi
    grep -F 'target backend not yet implemented' "$temporary_dir/$target.err" >/dev/null
    test ! -e "$temporary_dir/$target.s"
done
echo "PASS: AArch64/LoongArch64 target identities registered; incomplete emitters fail closed"

if "$MINIC_CC" --target=unknown-target -S "$temporary_dir/basic.i" \
        -o "$temporary_dir/unknown.s" 2>"$temporary_dir/unknown.err"; then
    echo "FAIL: unknown target unexpectedly accepted" >&2
    exit 1
fi
grep -F 'unsupported target' "$temporary_dir/unknown.err" >/dev/null
test ! -e "$temporary_dir/unknown.s"

echo "PASS: target selection M0; RV64 default parity; x86 signed-char semantics and codegen"
