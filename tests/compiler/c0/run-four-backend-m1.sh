#!/bin/sh
# Same C fixtures and oracle logic; only target toolchains differ.
set -eu
: "$MINIC_CC"
AARCH64_GCC=aarch64-linux-gnu-gcc
LOONGARCH64_GCC=loongarch64-linux-gnu-gcc-13
QEMU_AARCH64=qemu-aarch64
QEMU_LOONGARCH64=qemu-loongarch64
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
cat > "$w/return.i" <<'EOF'
int main(void) { return 42; }
EOF
cat > "$w/add.i" <<'EOF'
int mini_add(int a, int b) { return a + b; }
EOF
cat > "$w/calc.i" <<'EOF'
int mini_calc(int a, int b) { return a * b - a; }
EOF
cat > "$w/driver_add.c" <<'EOF'
extern int mini_add(int, int);
int main(void) { return mini_add(17, 25); }
EOF
cat > "$w/driver_calc.c" <<'EOF'
extern int mini_calc(int, int);
int main(void) { return mini_calc(6, 8); }
EOF
cat > "$w/unsupported.i" <<'EOF'
static int global_state = 42;
int main(void) { return global_state; }
EOF
check_qemu_42() {
    emulator=$1
    binary=$2
    set +e
    "$emulator" "$binary"
    result=$?
    set -e
    if [ "$result" -ne 42 ]; then
        echo "FOUR_BACKEND_M1=FAIL emulator=$emulator bin=$binary actual=$result expected=42" >&2
        return 1
    fi
}
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=$AARCH64_GCC; qemu=$QEMU_AARCH64 ;;
        loongarch64) gcc=$LOONGARCH64_GCC; qemu=$QEMU_LOONGARCH64 ;;
        *) exit 2 ;;
    esac
    command -v "$gcc" >/dev/null
    command -v "$qemu" >/dev/null
    echo "FOUR_BACKEND_M1_TOOLCHAIN target=$target compiler=$("$gcc" -dumpfullversion) qemu=$qemu"
    "$gcc" -static -x c "$w/return.i" -o "$w/$target.gnu.elf"
    check_qemu_42 "$qemu" "$w/$target.gnu.elf"
    for testcase in return add calc; do
        "$MINIC_CC" --target="$target-linux" -S "$w/$testcase.i" \
            -o "$w/$target.$testcase.s"
        test -s "$w/$target.$testcase.s"
        "$gcc" -c "$w/$target.$testcase.s" -o "$w/$target.$testcase.o"
        case "$testcase" in
            return) "$gcc" -static "$w/$target.return.o" -o "$w/$target.return.elf" ;;
            add) "$gcc" -static "$w/$target.add.o" "$w/driver_add.c" -o "$w/$target.add.elf" ;;
            calc) "$gcc" -static "$w/$target.calc.o" "$w/driver_calc.c" -o "$w/$target.calc.elf" ;;
        esac
        check_qemu_42 "$qemu" "$w/$target.$testcase.elf"
        echo "FOUR_BACKEND_M1_CASE=PASS target=$target case=$testcase MiniC_object=1 GNU_AS_LD=1 QEMU_exit=42"
    done
    if "$MINIC_CC" --target="$target-linux" -S "$w/unsupported.i" \
            -o "$w/$target.unsupported.s" 2>"$w/$target.unsupported.err"; then
        echo "FOUR_BACKEND_M1=FAIL target=$target admitted unsupported global" >&2
        exit 1
    fi
    test ! -e "$w/$target.unsupported.s"
    echo "FOUR_BACKEND_M1_TARGET=PASS target=$target GCC_oracle=1 MiniC_GNU_link_QEMU=3 unsupported=fail-closed"
done
echo "FOUR_BACKEND_M1=PASS architectures=2 executable_QEMU_cases=6 GCC_independent_oracles=2"
