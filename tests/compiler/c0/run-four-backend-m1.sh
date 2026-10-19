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
m2_enabled=0
if [ "${FOUR_BACKEND_M2:-0}" = "1" ] || [ "${FOUR_BACKEND_M2_STACK:-0}" = "1" ] || [ "${FOUR_BACKEND_M3_CALL:-0}" = "1" ] || [ "${FOUR_BACKEND_M4_GLOBAL:-0}" = "1" ] || [ "${FOUR_BACKEND_M5_POINTER:-0}" = "1" ] || [ "${FOUR_BACKEND_M6_ADDRESS:-0}" = "1" ]; then
    m2_enabled=1
fi
cat > "$w/return.i" <<'EOF'
int main(void) { return 42; }
EOF
# Requires two 16-bit chunks on AArch64 (cannot be a single mov).
cat > "$w/wide.i" <<'EOF'
int main(void) { return 1000000; }
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
# M2 reuses the exact same cross GNU AS/LD/QEMU scaffold as M1.
# First real Core CFG: both conditional successors + a backward loop.
cat > "$w/branch.i" <<'EOF'
int mini_branch(int take, int value) {
    if (take) return value;
    return 0;
}
EOF
cat > "$w/driver_branch.c" <<'EOF'
extern int mini_branch(int, int);
int main(void) {
    return mini_branch(1, 42) + mini_branch(0, 99);
}
EOF
cat > "$w/loop.i" <<'EOF'
int mini_loop(int n) {
    int count = 0;
    while (n) {
        count = count + 1;
        n = n - 1;
    }
    return count;
}
EOF
cat > "$w/driver_loop.c" <<'EOF'
extern int mini_loop(int);
int main(void) { return mini_loop(7) * 6 + mini_loop(0); }
EOF
cat > "$w/compare.i" <<'EOF'
int mini_lt(int a, int b) { return a < b; }
int mini_eq(int a, int b) { return a == b; }
EOF
cat > "$w/driver_compare.c" <<'EOF'
extern int mini_lt(int, int);
extern int mini_eq(int, int);
int main(void) {
    return mini_lt(-2, 4) * 20 + mini_eq(9, 9) * 22
         + mini_lt(4, -2) + mini_eq(3, 8);
}
EOF
# Relocatable pointer data is not supported yet, so the old M1 negative
# test remains fail-closed even after M4 accepts plain integer globals.
cat > "$w/unsupported.i" <<'EOF'
static int original = 42;
static int *global_pointer = &original;
int main(void) { return *global_pointer; }
EOF
cat > "$w/global_int.i" <<'EOF'
static int global_state = 40;
int mini_global_int(void) { return global_state + 2; }
EOF
cat > "$w/driver_global_int.c" <<'EOF'
extern int mini_global_int(void);
int main(void) { return mini_global_int(); }
EOF
# GNU consumes the actual symbol and bytes emitted by MiniC; this
# isolates ELF flat byte-array initialization from not-yet-supported
# pointer arithmetic, byte-load Core instructions and int conversion.
cat > "$w/global_array.i" <<'EOF'
const unsigned char mini_table[4] = { 7, 8, 9, 10 };
int mini_global_array(void) { return 42; }
EOF
cat > "$w/driver_global_array.c" <<'EOF'
extern const unsigned char mini_table[4];
extern int mini_global_array(void);
int main(void) {
    if (mini_table[0] != 7 || mini_table[1] != 8 ||
        mini_table[2] != 9 || mini_table[3] != 10)
        return 1;
    return mini_global_array();
}
EOF
# M2-stack ABI extension: the ninth and tenth original C integer
# parameters are supplied by GNU at incoming sp+0 and sp+8, not registers.
cat > "$w/stack9.i" <<'EOF'
int mini_stack9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    return a + b + c + d + e + f + g + h + i;
}
EOF
cat > "$w/driver_stack9.c" <<'EOF'
extern int mini_stack9(int, int, int, int, int, int, int, int, int);
int main(void) { return mini_stack9(1, 2, 3, 4, 5, 6, 7, 8, 6); }
EOF
cat > "$w/stack10.i" <<'EOF'
int mini_stack10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    return a + b + c + d + e + f + g + h + i + j;
}
EOF
cat > "$w/driver_stack10.c" <<'EOF'
extern int mini_stack10(int, int, int, int, int, int, int, int, int, int);
int main(void) { return mini_stack10(1, 2, 3, 4, 5, 6, 7, 8, 9, -3); }
EOF
# Mirror the x86 introduction order: first prove a small direct-call
# Core/ABI runtime seam, then pressure it with original tinyAES/cJSON.
cat > "$w/call_external.i" <<'EOF'
extern int gnu_twice(int);
int mini_external(int x) { return gnu_twice(x) + gnu_twice(1); }
EOF
cat > "$w/driver_call_external.c" <<'EOF'
extern int mini_external(int);
int gnu_twice(int n) { return n * 2; }
int main(void) { return mini_external(20); }
EOF
cat > "$w/call_internal.i" <<'EOF'
static int mini_helper(int a, int b) { return a + b; }
int mini_internal(int x) { return mini_helper(x, 2) + 1; }
EOF
cat > "$w/driver_call_internal.c" <<'EOF'
extern int mini_internal(int);
int main(void) { return mini_internal(39); }
EOF
cat > "$w/call_nested.i" <<'EOF'
extern int gnu_twice(int);
int mini_nested(int n) {
    if (n == 0) return 2;
    return gnu_twice(20) + mini_nested(n - 1);
}
EOF
cat > "$w/driver_call_nested.c" <<'EOF'
extern int mini_nested(int);
int gnu_twice(int n) { return n * 2; }
int main(void) { return mini_nested(1); }
EOF
cat > "$w/call_unsupported.i" <<'EOF'
extern int gnu_nine(int,int,int,int,int,int,int,int,int);
int mini_unsupported(void) { return gnu_nine(1,2,3,4,5,6,7,8,9); }
EOF
# Following the first genuine AES_init_ctx pointer/void ABI frontier.
cat > "$w/pointer_read.i" <<'EOF'
int mini_pointer_read(int *p) { return *p; }
EOF
cat > "$w/driver_pointer_read.c" <<'EOF'
extern int mini_pointer_read(int *);
int main(void) { int v = 42; return mini_pointer_read(&v); }
EOF
cat > "$w/pointer_void.i" <<'EOF'
static void mini_write_pointer(int *p) { *p = 42; }
int mini_pointer_void(int *p) { mini_write_pointer(p); return *p; }
EOF
cat > "$w/driver_pointer_void.c" <<'EOF'
extern int mini_pointer_void(int *);
int main(void) { int v = 0; return mini_pointer_void(&v); }
EOF
cat > "$w/pointer_return.i" <<'EOF'
int *mini_pointer_return(int *p) { return p; }
EOF
cat > "$w/driver_pointer_return.c" <<'EOF'
extern int *mini_pointer_return(int *);
int main(void) {
    int v = 42;
    int *ptr = mini_pointer_return(&v);
    return ptr == &v ? *ptr : 1;
}
EOF
# M6: the first actual original tinyAES Core path uses
# field.addr on struct AES_ctx and pointer.offset to the first array byte.
# These unchanged-C ABI tests target real ELF reads/writes, not only .s.
cat > "$w/field_read.i" <<'EOF'
struct Pair { int left; int right; };
int mini_field_read(struct Pair *p) { return p->right; }
EOF
cat > "$w/driver_field_read.c" <<'EOF'
struct Pair { int left; int right; };
extern int mini_field_read(struct Pair *);
int main(void) { struct Pair p = { 7, 42 }; return mini_field_read(&p); }
EOF
cat > "$w/field_write.i" <<'EOF'
struct Pair { int left; int right; };
void mini_field_write(struct Pair *p) { p->right = 42; }
EOF
cat > "$w/driver_field_write.c" <<'EOF'
struct Pair { int left; int right; };
extern void mini_field_write(struct Pair *);
int main(void) {
    struct Pair p = { 4, 0 };
    mini_field_write(&p);
    return p.left == 4 ? p.right : 1;
}
EOF
cat > "$w/pointer_index.i" <<'EOF'
int mini_pointer_index(int *p, int index) { return p[index]; }
EOF
cat > "$w/driver_pointer_index.c" <<'EOF'
extern int mini_pointer_index(int *, int);
int main(void) {
    int values[4] = { 9, 18, 42, 70 };
    return mini_pointer_index(values, 2);
}
EOF
cat > "$w/pointer_negative.i" <<'EOF'
int mini_pointer_negative(int *p, int offset) { return p[offset]; }
EOF
cat > "$w/driver_pointer_negative.c" <<'EOF'
extern int mini_pointer_negative(int *, int);
int main(void) {
    int values[3] = { 8, 42, 17 };
    return mini_pointer_negative(&values[2], -1);
}
EOF
cat > "$w/pointer_cast.i" <<'EOF'
int mini_pointer_cast(void *p) {
    int *q = (int *)p;
    return *q;
}
EOF
cat > "$w/driver_pointer_cast.c" <<'EOF'
extern int mini_pointer_cast(void *);
int main(void) {
    int value = 42;
    return mini_pointer_cast((void *)&value);
}
EOF
check_qemu_exit() {
    emulator=$1
    binary=$2
    expected=$3
    set +e
    # Defend the shared regression runner against accidentally nonterminating
    # generated branch/loop code. QEMU exit code 124 is an explicit failure.
    timeout 15s "$emulator" "$binary"
    result=$?
    set -e
    if [ "$result" -ne "$expected" ]; then
        echo "FOUR_BACKEND_M1=FAIL emulator=$emulator bin=$binary actual=$result expected=$expected" >&2
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
    check_qemu_exit "$qemu" "$w/$target.gnu.elf" 42
    "$gcc" -static -x c "$w/wide.i" -o "$w/$target.gnu-wide.elf"
    check_qemu_exit "$qemu" "$w/$target.gnu-wide.elf" 64
    cases="return add calc"
    if [ "$m2_enabled" = "1" ]; then cases="$cases branch loop compare"; fi
    if [ "${FOUR_BACKEND_M2_STACK:-0}" = "1" ]; then
        cases="$cases stack9 stack10"
    fi
    if [ "${FOUR_BACKEND_M3_CALL:-0}" = "1" ]; then
        cases="$cases call_external call_internal call_nested"
    fi
    if [ "${FOUR_BACKEND_M4_GLOBAL:-0}" = "1" ]; then
        cases="$cases global_int global_array"
    fi
    if [ "${FOUR_BACKEND_M5_POINTER:-0}" = "1" ]; then
        cases="$cases pointer_read pointer_void pointer_return"
    fi
    if [ "${FOUR_BACKEND_M6_ADDRESS:-0}" = "1" ]; then
        cases="$cases field_read field_write pointer_index pointer_negative pointer_cast"
    fi
    if [ "$m2_enabled" = "1" ]; then
        # Independent originals for every new CFG/comparison fixture, not
        # merely an expected return code from a MiniC-owned executable.
        oracle_cases="branch loop compare"
        if [ "${FOUR_BACKEND_M2_STACK:-0}" = "1" ]; then
            oracle_cases="$oracle_cases stack9 stack10"
        fi
        if [ "${FOUR_BACKEND_M3_CALL:-0}" = "1" ]; then
            oracle_cases="$oracle_cases call_external call_internal call_nested"
        fi
        if [ "${FOUR_BACKEND_M4_GLOBAL:-0}" = "1" ]; then
            oracle_cases="$oracle_cases global_int global_array"
        fi
        if [ "${FOUR_BACKEND_M5_POINTER:-0}" = "1" ]; then
            oracle_cases="$oracle_cases pointer_read pointer_void pointer_return"
        fi
        if [ "${FOUR_BACKEND_M6_ADDRESS:-0}" = "1" ]; then
            oracle_cases="$oracle_cases field_read field_write pointer_index pointer_negative pointer_cast"
        fi
        for testcase in $oracle_cases; do
            "$gcc" -static -x c "$w/$testcase.i" "$w/driver_$testcase.c" \
                -o "$w/$target.gnu-$testcase.elf"
            check_qemu_exit "$qemu" "$w/$target.gnu-$testcase.elf" 42
            echo "FOUR_BACKEND_M2_GNU_ORACLE=PASS target=$target case=$testcase GCC_executable=1 QEMU_exit=42"
        done
    fi
    for testcase in $cases; do
        MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" -S "$w/$testcase.i" \
            -o "$w/$target.$testcase.s"
        test -s "$w/$target.$testcase.s"
        "$gcc" -c "$w/$target.$testcase.s" -o "$w/$target.$testcase.o"
        case "$testcase" in
            return) "$gcc" -static "$w/$target.return.o" -o "$w/$target.return.elf" ;;
            add) "$gcc" -static "$w/$target.add.o" "$w/driver_add.c" -o "$w/$target.add.elf" ;;
            calc) "$gcc" -static "$w/$target.calc.o" "$w/driver_calc.c" -o "$w/$target.calc.elf" ;;
            branch) "$gcc" -static "$w/$target.branch.o" "$w/driver_branch.c" -o "$w/$target.branch.elf" ;;
            loop) "$gcc" -static "$w/$target.loop.o" "$w/driver_loop.c" -o "$w/$target.loop.elf" ;;
            compare) "$gcc" -static "$w/$target.compare.o" "$w/driver_compare.c" -o "$w/$target.compare.elf" ;;
            stack9) "$gcc" -static "$w/$target.stack9.o" "$w/driver_stack9.c" -o "$w/$target.stack9.elf" ;;
            stack10) "$gcc" -static "$w/$target.stack10.o" "$w/driver_stack10.c" -o "$w/$target.stack10.elf" ;;
            field_read|field_write|pointer_index|pointer_negative|pointer_cast)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            pointer_read|pointer_void|pointer_return)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            global_int|global_array)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            call_external|call_internal|call_nested)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
        esac
        check_qemu_exit "$qemu" "$w/$target.$testcase.elf" 42
        echo "FOUR_BACKEND_M1_CASE=PASS target=$target case=$testcase MiniC_object=1 GNU_AS_LD=1 QEMU_exit=42"
    done
    "$MINIC_CC" --target="$target-linux" -S "$w/wide.i" -o "$w/$target.wide.s"
    "$gcc" -c "$w/$target.wide.s" -o "$w/$target.wide.o"
    "$gcc" -static "$w/$target.wide.o" -o "$w/$target.wide.elf"
    check_qemu_exit "$qemu" "$w/$target.wide.elf" 64
    echo "FOUR_BACKEND_M1_CASE=PASS target=$target case=wide-immediate MiniC_object=1 GNU_AS_LD=1 QEMU_exit=64 GCC_oracle=64"
    if "$MINIC_CC" --target="$target-linux" -S "$w/unsupported.i" \
            -o "$w/$target.unsupported.s" 2>"$w/$target.unsupported.err"; then
        echo "FOUR_BACKEND_M1=FAIL target=$target admitted unsupported global" >&2
        exit 1
    fi
    test ! -e "$w/$target.unsupported.s"
    if [ "${FOUR_BACKEND_M3_CALL:-0}" = "1" ]; then
        if "$MINIC_CC" --target="$target-linux" -S "$w/call_unsupported.i" \
                -o "$w/$target.call_unsupported.s" 2>"$w/$target.call_unsupported.err"; then
            echo "FOUR_BACKEND_M3_CALL=FAIL target=$target accepted unsupported nine-argument caller" >&2
            exit 1
        fi
        test ! -e "$w/$target.call_unsupported.s"
        echo "FOUR_BACKEND_M3_CALL_TARGET=PASS target=$target MiniC_GNU_link_QEMU=10 direct_call_cases=3 GNU_oracles=3 unsupported_calls=fail-closed"
    fi
    if [ "$m2_enabled" = "1" ]; then
        echo "FOUR_BACKEND_M2_TARGET=PASS target=$target MiniC_GNU_link_QEMU=7 CFG_branch=1 CFG_loop=1 signed_compare=1 unsupported=fail-closed"
        if [ "${FOUR_BACKEND_M2_STACK:-0}" = "1" ]; then
            echo "FOUR_BACKEND_M2_STACK_TARGET=PASS target=$target MiniC_GNU_link_QEMU=9 GNU_stack_argument_oracles=2"
        fi
    else
        echo "FOUR_BACKEND_M1_TARGET=PASS target=$target GCC_oracle=2 MiniC_GNU_link_QEMU=4 unsupported=fail-closed"
    fi
done
if [ "$m2_enabled" = "1" ]; then
    echo "FOUR_BACKEND_M2=PASS architectures=2 executable_QEMU_cases=14 CFG_cases=4 comparison_cases=2 GCC_independent_oracles=10"
    if [ "${FOUR_BACKEND_M2_STACK:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M2_STACK=PASS architectures=2 executable_QEMU_cases=18 ninth_tenth_arg_cases=4 GCC_independent_oracles=14"
    fi
    if [ "${FOUR_BACKEND_M3_CALL:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M3_CALL=PASS architectures=2 executable_QEMU_cases=20 direct_calls=6 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M4_GLOBAL:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M4_GLOBAL=PASS architectures=2 executable_QEMU_cases=18 global_cases=4 GNU_independent_oracles=14 unsupported_relocations=fail-closed"
    fi
    if [ "${FOUR_BACKEND_M5_POINTER:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M5_POINTER=PASS architectures=2 executable_QEMU_cases=20 pointer_void_cases=6 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M6_ADDRESS:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M6_ADDRESS=PASS architectures=2 executable_QEMU_cases=24 field_pointer_bitcast_cases=10 GNU_independent_oracles=20"
    fi
else
    echo "FOUR_BACKEND_M1=PASS architectures=2 executable_QEMU_cases=8 GCC_independent_oracles=4"
fi
