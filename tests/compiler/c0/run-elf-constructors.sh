#!/bin/sh
# ELF GNU constructor semantics: real startup side-effect, not compile-only.
set -eux
: "${MINIC_CC:?MINIC_CC must name the built minic-cc}"
: "${HOST_CC:=cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/start.i" <<'EOF'
static volatile int start_value;
static void __attribute__((constructor)) register_one(void) {
    start_value += 17;
}
static void __attribute__((__constructor__)) register_two(void) {
    start_value += 25;
}
int main(void) {
    return start_value == 42 ? 42 : 3;
}
EOF
"$MINIC_CC" --target=x86_64-linux -S "$work/start.i" -o "$work/start.x86.s"
"$HOST_CC" -c -x assembler "$work/start.x86.s" -o "$work/start.x86.o"
readelf -SW "$work/start.x86.o" | grep -qE '[.]init_array'
readelf -rW "$work/start.x86.o" | grep -qE '[.]rela.init_array'
"$HOST_CC" -no-pie "$work/start.x86.o" -o "$work/start.x86.bin"
"$HOST_CC" -std=gnu11 -no-pie -x c "$work/start.i" -o "$work/start.gnu.bin"
set +e
"$work/start.x86.bin"; x86_rc=$?
"$work/start.gnu.bin"; gnu_rc=$?
set -e
test "$x86_rc" -eq 42 && test "$gnu_rc" -eq 42
echo "X86_CONSTRUCTOR_ELF=PASS init_array_relocations=2 before_main=1 gcc_oracle=1"
# Same Core/AST semantic owner must serialize the startup address for RV64.
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-readelf >/dev/null 2>&1; then
    "$MINIC_CC" --target=riscv64-linux -S "$work/start.i" -o "$work/start.rv64.s"
    riscv64-linux-gnu-gcc -c -x assembler "$work/start.rv64.s" -o "$work/start.rv64.o"
    riscv64-linux-gnu-readelf -SW "$work/start.rv64.o" | grep -qE '[.]init_array'
    riscv64-linux-gnu-readelf -rW "$work/start.rv64.o" | grep -qE '[.]rela.init_array'
    if command -v qemu-riscv64 >/dev/null 2>&1; then
        riscv64-linux-gnu-gcc -static "$work/start.rv64.o" -o "$work/start.rv64.bin"
        riscv64-linux-gnu-gcc -static -x c "$work/start.i" -o "$work/start.rv64.gnu.bin"
        set +e
        qemu-riscv64 "$work/start.rv64.bin"; rv64_rc=$?
        qemu-riscv64 "$work/start.rv64.gnu.bin"; rv_gnu_rc=$?
        set -e
        test "$rv64_rc" -eq 42 && test "$rv_gnu_rc" -eq 42
        echo "RV64_CONSTRUCTOR_ELF=PASS init_array_relocations=2 qemu_before_main=1 gcc_oracle=1"
    else
        echo "RV64_CONSTRUCTOR_ELF=PASS init_array_relocations=2 qemu=UNAVAILABLE"
    fi
else
    echo "RV64_CONSTRUCTOR_ELF=SKIP cross-tools-unavailable"
fi
cat > "$work/priority.i" <<'EOF'
static void __attribute__((constructor(101))) should_fail(void) {}
int main(void) { return 0; }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$work/priority.i" -o "$work/bad.s" >"$work/priority.out" 2>"$work/priority.err"; then
    echo "CONSTRUCTOR_NEGATIVE=FAIL unsupported-priority-was-accepted" >&2
    exit 1
fi
cat > "$work/badsig.i" <<'EOF'
static int __attribute__((constructor)) should_fail(int x) { return x; }
int main(void) { return 0; }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$work/badsig.i" -o "$work/bad.s" >"$work/badsig.out" 2>"$work/badsig.err"; then
    echo "CONSTRUCTOR_NEGATIVE=FAIL wrong-signature-was-accepted" >&2
    exit 1
fi
echo "CONSTRUCTOR_NEGATIVE=PASS unsupported-priority=fail-closed bad-signature=fail-closed"
