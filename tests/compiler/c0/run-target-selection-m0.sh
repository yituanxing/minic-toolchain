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
# functions. Keep the type distinct from long double even when both use
# IEEE binary128 storage (RV64), or when x86 uses 80-bit long double.
# Target selection must not regress now that RV64 has real binary128 ABI
# qualification; mere declaration is not proof that every operation works.
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
"$MINIC_CC" --target=riscv64-linux -S "$temporary_dir/float128-prototype.i" \
    -o "$temporary_dir/float128-rv.s"
grep -F 'main:' "$temporary_dir/float128-rv.s" >/dev/null

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
"$MINIC_CC" --target=riscv64-linux -S \
    "$temporary_dir/extended-float-prototypes.i" \
    -o "$temporary_dir/rv-extended-float.s"
grep -F 'main:' "$temporary_dir/rv-extended-float.s" >/dev/null
echo "PASS: x86/RV64 extended floating type identities and target layout"

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

# M1 replaces M0's previously necessary missing-backend rejection:
# GNU-assembler-ready instructions are now produced by distinct ISA emitters.
# Actual ELF link/QEMU/GCC differentiation is checked in four-backend M1.
"$MINIC_CC" --target=aarch64-linux -S "$temporary_dir/basic.i" \
    -o "$temporary_dir/aarch64.s"
"$MINIC_CC" --target=loongarch64-linux -S "$temporary_dir/basic.i" \
    -o "$temporary_dir/loongarch64.s"
grep -F 'movz w9, #42' "$temporary_dir/aarch64.s" >/dev/null
grep -F 'li.w $t0, 42' "$temporary_dir/loongarch64.s" >/dev/null
echo "PASS: ARM64 and LoongArch64 emit distinct Core integer-return assembly; target M1"

# RV32I/ILP32 now has a real independently qualified ELF32 backend.
# Check positive ISA selection and distinguish it from the still-invalid
# RV64 ISA/ABI combination. Never allow a fallback into RV64 machine code.
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$temporary_dir/basic.i" -o "$temporary_dir/rv32.s"
grep -F 'main:' "$temporary_dir/rv32.s" >/dev/null
grep -F '.option norvc' "$temporary_dir/rv32.s" >/dev/null
grep -F 'sw ra,' "$temporary_dir/rv32.s" >/dev/null
if grep -E '(^|[[:space:]])(sd|ld|addw|subw|mul|div|rem|fadd\.|vsetvli)([[:space:]]|$)' \
        "$temporary_dir/rv32.s" >/dev/null; then
    echo "FAIL: strict RV32I emitted an incompatible/non-I instruction" >&2
    exit 1
fi
if "$MINIC_CC" --target=riscv32-linux -march=rv64i -mabi=lp64 \
        -S "$temporary_dir/basic.i" -o "$temporary_dir/rv32-mixed.s" \
        2>"$temporary_dir/rv32-mixed.err"; then
    echo "FAIL: RV32 must reject RV64 ISA/ABI" >&2
    exit 1
fi
grep -F 'invalid RISC-V configuration' "$temporary_dir/rv32-mixed.err" >/dev/null
test ! -e "$temporary_dir/rv32-mixed.s"
echo 'RV32_TARGET_GATE=PASS model_registered=1 strict_rv32i_emitted=1 mixed_isa_rejected=1'

if "$MINIC_CC" --target=unknown-target -S "$temporary_dir/basic.i" \
        -o "$temporary_dir/unknown.s" 2>"$temporary_dir/unknown.err"; then
    echo "FAIL: unknown target unexpectedly accepted" >&2
    exit 1
fi
grep -F 'unsupported target' "$temporary_dir/unknown.err" >/dev/null
test ! -e "$temporary_dir/unknown.s"

echo "PASS: target selection M0; RV64 default parity; x86 signed-char semantics and codegen"
