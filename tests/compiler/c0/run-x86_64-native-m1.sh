#!/bin/sh
# x86-64 M1: native Core integer/ABI smoke, no additional CI workflow.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc}"
: "${HOST_CC:=cc}"

case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "SKIP: x86-64 native M1 smoke requires an x86-64 host"; exit 0 ;;
esac

tempdir=$(mktemp -d)
trap 'rm -rf "$tempdir"' EXIT HUP INT TERM

check_exit() {
  name="$1"
  expected="$2"
  "$MINIC_CC" --target=x86_64-linux -S "$tempdir/$name.i" -o "$tempdir/$name.s"
  "$HOST_CC" -no-pie -x assembler "$tempdir/$name.s" -o "$tempdir/$name.bin"
  set +e
  "$tempdir/$name.bin"
  code="$?"
  set -e
  if [ "$code" -ne "$expected" ]; then
    echo "FAIL: $name actual=$code expected=$expected" >&2
    exit 1
  fi
  echo "PASS: x86-64 $name exit=$code"
}

cat > "$tempdir/return42.i" <<'EOF'
int main(void) { return 42; }
EOF
check_exit return42 42

# Original Linux x86-64: register unsigned long current_stack_pointer
# asm("rsp"). GCC binds the *live stack pointer*, after the callee's prologue,
# rather than materializing an ELF global or substituting the saved rbp.
# Verify real GNU assembly and execute natively against an actual local.
cat > "$tempdir/x86_rsp_live.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
int main(void) {
    volatile unsigned long stack_local = 42UL;
    unsigned long rsp_snapshot = current_stack_pointer;
    unsigned long object_address = (unsigned long)&stack_local;
    if (rsp_snapshot == 0UL || rsp_snapshot > object_address)
        return 1;
    if (object_address - rsp_snapshot > 4096UL)
        return 2;
    if ((rsp_snapshot & 15UL) != 0UL)
        return 3;
    return (int)stack_local;
}
EOF
check_exit x86_rsp_live 42
grep -F '  movq %rsp, %rax' "$tempdir/x86_rsp_live.s" >/dev/null
if grep -F 'current_stack_pointer@' "$tempdir/x86_rsp_live.s" >/dev/null; then
    echo 'FAIL: x86 rsp binding lowered as a global symbol' >&2
    exit 1
fi
cat > "$tempdir/x86_rsp_unqualified.i" <<'EOF'
register unsigned long unqualified_reg asm("r15");
unsigned long probe(void) { return unqualified_reg; }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/x86_rsp_unqualified.i" \
    -o "$tempdir/x86_rsp_unqualified.s" >"$tempdir/x86_rsp_unqualified.out" \
    2>"$tempdir/x86_rsp_unqualified.err"; then
    echo 'FAIL: x86 unqualified r15 binding accepted' >&2
    exit 1
fi
grep -F 'fixed register binding is not supported by this target' \
    "$tempdir/x86_rsp_unqualified.err" >/dev/null

# The unchanged x86 Linux asm helper mul_u64_u64_div_u64() is
# NONvolatile GNU output ASM: mulq %2; divq %3 with =a/a/rm/rm and
# physical rdx clobber. Reuse the current Core Structured ASM and x86
# Backend Contract. GNU and MiniC must both materialize the same quotient
# including a nonzero high half in RDX; never silently skip the asm.
cat > "$tempdir/x86_gnu_muldiv.i" <<'EOF'
static unsigned long long muldiv_u64(unsigned long long a,
                                     unsigned long long mul,
                                     unsigned long long div) {
    unsigned long long q;
    asm ("mulq %2; divq %3" : "=a" (q)
        : "a" (a), "rm" (mul), "rm" (div) : "rdx");
    return q;
}
int main(void) {
    if (muldiv_u64(0x8000000000000000ULL, 2ULL, 3ULL) !=
        6148914691236517205ULL)
        return 1;
    return (int)muldiv_u64(21ULL, 4ULL, 2ULL);
}
EOF
check_exit x86_gnu_muldiv 42
grep -F '  mulq %r10' "$tempdir/x86_gnu_muldiv.s" >/dev/null
grep -F '  divq %r11' "$tempdir/x86_gnu_muldiv.s" >/dev/null
"$HOST_CC" -std=gnu11 -no-pie -x c "$tempdir/x86_gnu_muldiv.i" \
    -o "$tempdir/x86_gnu_muldiv.gnu"
set +e
"$tempdir/x86_gnu_muldiv.gnu"
muldiv_gcc_result=$?
set -e
test "$muldiv_gcc_result" -eq 42
echo 'PASS: x86 GNU 64x64/128-div asm MiniC/GCC differential, RDX clobber and true Core output'

# Nearby operand templates must remain rejected unless their full machine
# dataflow and physical-register constraints are separately qualified.
cat > "$tempdir/x86_gnu_muldiv_unknown.i" <<'EOF'
unsigned long long bad(unsigned long long x,
                       unsigned long long m,
                       unsigned long long d) {
    unsigned long long q;
    asm ("mulq %2; divq %3; nop" : "=a" (q)
         : "a" (x), "rm" (m), "rm" (d) : "rdx");
    return q;
}
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/x86_gnu_muldiv_unknown.i" \
    -o "$tempdir/x86_gnu_muldiv_unknown.s" \
    >"$tempdir/x86_gnu_muldiv_unknown.out" \
    2>"$tempdir/x86_gnu_muldiv_unknown.err"; then
    echo 'FAIL: unqualified x86 muldiv ASM template accepted' >&2
    exit 1
fi

# BusyBox allyesconfig + libselinux: a GNU deprecated typedef is an
# informational type attribute, not a different object/argument layout.
# Check both x86 runtime and RV64 parsing; retain diagnostic-only behavior.
cat > "$tempdir/typedef_deprecated.i" <<'EOF'
typedef char *LegacyPointer __attribute__((deprecated));
typedef int LegacyInteger __attribute__((__deprecated__));
int main(void) {
    LegacyPointer p = 0;
    LegacyInteger v = 42;
    return p == 0 ? v : 1;
}
EOF
check_exit typedef_deprecated 42
"$MINIC_CC" --target=riscv64-linux -S "$tempdir/typedef_deprecated.i" \
    -o "$tempdir/typedef_deprecated.rv64.s"
"$HOST_CC" -Wno-deprecated-declarations -std=gnu11 -no-pie -x c \
    "$tempdir/typedef_deprecated.i" -o "$tempdir/typedef_deprecated.gnu"
set +e
"$tempdir/typedef_deprecated.gnu"
gnu_rc=$?
set -e
test "$gnu_rc" -eq 42
echo "PASS: GNU diagnostic typedef attributes preserve x86 and RV64 C semantics"

# GNU may_alias is also used on BusyBox local pointer declarators, notably
# libbb/{signals,lineedit}.c and util-linux/mdev.c. MiniC does not use
# type-based alias optimizations; this spelling must retain ordinary layout.
cat > "$tempdir/may_alias_local.i" <<'EOF'
int main(void) {
    unsigned long data = 42UL;
    unsigned long *pointer __attribute__((__may_alias__)) = &data;
    return (int)*pointer;
}
EOF
check_exit may_alias_local 42
"$MINIC_CC" --target=riscv64-linux -S "$tempdir/may_alias_local.i" \
    -o "$tempdir/may_alias_local.rv64.s"
"$HOST_CC" -std=gnu11 -no-pie -x c "$tempdir/may_alias_local.i" \
    -o "$tempdir/may_alias_local.gnu"
set +e
"$tempdir/may_alias_local.gnu"
may_alias_gcc_rc=$?
set -e
test "$may_alias_gcc_rc" -eq 42
echo "PASS: GNU no-argument may_alias local pointer x86/RV64/GCC oracle"

# BusyBox procps/top.c has function parameter [MI_MAX], where MI_MAX is
# an enumerator, not the name of a preceding parameter. GCC treats it as
# a fixed ICE bound and adjusts the outer array parameter to a pointer.
cat > "$tempdir/array_param_enum_ice.i" <<'EOF'
enum { MI_A, MI_B, MI_MAX };
static unsigned long enum_array(unsigned long values[MI_MAX]) {
    return values[MI_A] + values[MI_B];
}
int main(void) {
    unsigned long values[MI_MAX] = {17UL, 25UL};
    return (int)enum_array(values);
}
EOF
check_exit array_param_enum_ice 42
"$MINIC_CC" --target=riscv64-linux -S "$tempdir/array_param_enum_ice.i" \
    -o "$tempdir/array_param_enum_ice.rv64.s"
"$HOST_CC" -std=gnu11 -no-pie -x c "$tempdir/array_param_enum_ice.i" \
    -o "$tempdir/array_param_enum_ice.gnu"
set +e
"$tempdir/array_param_enum_ice.gnu"
enum_array_gcc_rc=$?
set -e
test "$enum_array_gcc_rc" -eq 42
echo "PASS: C enum ICE array outer parameter adjusts to pointer on x86/RV64/GCC"


cat > "$tempdir/params_call.i" <<'EOF'
int sum(int a, int b) { return a + b; }
int main(void) { return sum(17, 25); }
EOF
check_exit params_call 42

# SQLite's util.c uses genuine __uint128_t, including nonzero upper
# halves. This is a native two-limb test, not a 64-bit fallback.
cat > "$tempdir/sqlite_u128.i" <<'EOF'
typedef unsigned __int128 U128;
typedef __int128 I128;
int main(void) {
    U128 a = ((U128)1 << 64) + 7;
    U128 b = (U128)0xffffffffffffffffULL * (U128)0xffffffffffffffffULL;
    U128 c = ((U128)1 << 64) - 1;
    U128 d = c + 2;
    U128 e = (U128)0x8000000000000000ULL * (U128)3;
    volatile U128 memory = d;
    I128 negative = (I128)(-2LL);
    if ((unsigned long long)(a >> 64) != 1ULL ||
        (unsigned long long)a != 7ULL) return 1;
    if ((unsigned long long)(b >> 64) != 0xfffffffffffffffeULL ||
        (unsigned long long)b != 1ULL) return 2;
    if ((unsigned long long)(memory >> 64) != 1ULL ||
        (unsigned long long)memory != 1ULL) return 3;
    if ((unsigned long long)(e >> 64) != 1ULL ||
        (unsigned long long)e != 0x8000000000000000ULL) return 4;
    if ((unsigned long long)((U128)negative >> 64) !=
        0xffffffffffffffffULL) return 5;
    if ((unsigned long long)((a - 7) >> 64) != 1ULL) return 6;
    if ((unsigned long long)(((U128)1 << 100) >> 64) != (1ULL << 36)) return 7;
    return 42;
}
EOF
check_exit sqlite_u128 42

# Check the same source against the host GNU compiler.
"$HOST_CC" -std=gnu11 -no-pie -x c "$tempdir/sqlite_u128.i" -o "$tempdir/sqlite_u128.gnu"
set +e
"$tempdir/sqlite_u128.gnu"
native_result=$?
set -e
test "$native_result" -eq 42
echo "PASS: GCC oracle sqlite_u128 exit=42"

# The unsigned 128-bit SysV return convention is RDX:RAX, not
# a hidden pointer. Check MiniC and GNU interoperability in both directions.
cat > "$tempdir/sqlite_u128_abi_mini.i" <<'EOF'
typedef unsigned __int128 U128;
U128 minic_u128_return(void) {
    return ((U128)0xfedcba9876543210ULL << 64) | (U128)0x123456789abcdef0ULL;
}
EOF
cat > "$tempdir/sqlite_u128_abi_host.c" <<'EOF'
typedef unsigned __int128 U128;
extern U128 minic_u128_return(void);
int main(void) {
    U128 value = minic_u128_return();
    return ((unsigned long long)(value >> 64) == 0xfedcba9876543210ULL &&
            (unsigned long long)value == 0x123456789abcdef0ULL) ? 42 : 1;
}
EOF
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/sqlite_u128_abi_mini.i" \
    -o "$tempdir/sqlite_u128_abi_mini.s"
"$HOST_CC" -c -x assembler "$tempdir/sqlite_u128_abi_mini.s" \
    -o "$tempdir/sqlite_u128_abi_mini.o"
"$HOST_CC" -no-pie "$tempdir/sqlite_u128_abi_host.c" \
    "$tempdir/sqlite_u128_abi_mini.o" -o "$tempdir/sqlite_u128_abi_mini.bin"
set +e
"$tempdir/sqlite_u128_abi_mini.bin"
native_result=$?
set -e
test "$native_result" -eq 42
echo "PASS: x86-64 __uint128_t GNU caller / MiniC callee return ABI"

cat > "$tempdir/sqlite_u128_abi_gnu.i" <<'EOF'
typedef unsigned __int128 U128;
extern U128 gcc_u128_return(void);
int main(void) {
    U128 value = gcc_u128_return();
    return ((unsigned long long)(value >> 64) == 0x123456789abcdef0ULL &&
            (unsigned long long)value == 0xfedcba9876543210ULL) ? 42 : 1;
}
EOF
cat > "$tempdir/sqlite_u128_abi_receiver.c" <<'EOF'
typedef unsigned __int128 U128;
U128 gcc_u128_return(void) {
    return ((U128)0x123456789abcdef0ULL << 64) | (U128)0xfedcba9876543210ULL;
}
EOF
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/sqlite_u128_abi_gnu.i" \
    -o "$tempdir/sqlite_u128_abi_gnu.s"
"$HOST_CC" -c -x assembler "$tempdir/sqlite_u128_abi_gnu.s" \
    -o "$tempdir/sqlite_u128_abi_gnu.o"
"$HOST_CC" -no-pie "$tempdir/sqlite_u128_abi_receiver.c" \
    "$tempdir/sqlite_u128_abi_gnu.o" -o "$tempdir/sqlite_u128_abi_gnu.bin"
set +e
"$tempdir/sqlite_u128_abi_gnu.bin"
native_result=$?
set -e
test "$native_result" -eq 42
echo "PASS: x86-64 __uint128_t MiniC caller / GNU callee return ABI"

cat > "$tempdir/sqlite_u128_shift.i" <<'EOF'
typedef unsigned __int128 U128;
typedef __int128 I128;
int main(void) {
    U128 a = ((U128)0xabcULL << 64) | 0xdefULL;
    I128 negative = (I128)(-4);
    if ((unsigned long long)(a >> 64) != 0xabcULL) return 1;
    if ((unsigned long long)(a >> 65) != (0xabcULL >> 1)) return 2;
    if ((unsigned long long)(a >> 63) != 0x1578ULL) return 3;
    if ((unsigned long long)(((U128)1 << 127) >> 64) !=
        0x8000000000000000ULL) return 4;
    if ((unsigned long long)((U128)negative >> 64) !=
        0xffffffffffffffffULL) return 5;
    if ((unsigned long long)(negative >> 65) !=
        0xffffffffffffffffULL) return 6;
    return 42;
}
EOF
check_exit sqlite_u128_shift 42
"$HOST_CC" -std=gnu11 -no-pie -x c "$tempdir/sqlite_u128_shift.i" \
    -o "$tempdir/sqlite_u128_shift.gnu"
set +e
"$tempdir/sqlite_u128_shift.gnu"
native_result=$?
set -e
test "$native_result" -eq 42
echo "PASS: GCC oracle sqlite_u128_shift exit=42"

# Preserve binary64-origin x87 literals without treating binary128 bits
# as an 80-bit value. The zero constant is also required for unary negate.
cat > "$tempdir/tinycc_x87_literals.i" <<'EOF'
int main(void) {
    volatile long double a = 1.25L;
    volatile long double zero = 0.0L;
    long double b = a + zero;
    if ((double)b != 1.25) return 1;
    if ((double)(-a) != -1.25) return 2;
    volatile long double nine = 9.0L;
    volatile long double two = 2.0L;
    if ((double)(nine / two) != 4.5) return 3;
    if ((double)(two - nine) != -7.0) return 4;
    if ((double)(-0.0L) != -0.0) return 5;
    return 42;
}
EOF
check_exit tinycc_x87_literals 42

# TinyCC gen_cast uses unsigned 64-bit to x87 conversion even for
# values above INT64_MAX. Check against exact arithmetic and GCC oracle.
cat > "$tempdir/tinycc_x87_uint64.i" <<'EOF'
int main(void) {
    volatile unsigned long long hi = 0xffffffffffffffffULL;
    volatile unsigned long long boundary = 0x8000000000000000ULL;
    volatile unsigned long long lo = 42ULL;
    long double a = (long double)hi;
    long double b = (long double)boundary;
    long double c = (long double)lo;
    if ((double)a != 18446744073709551616.0) return 1;
    if ((double)b != 9223372036854775808.0) return 2;
    if ((double)c != 42.0) return 3;
    return 42;
}
EOF
check_exit tinycc_x87_uint64 42
"$HOST_CC" -std=gnu11 -no-pie -x c "$tempdir/tinycc_x87_uint64.i" -o "$tempdir/tinycc_x87_uint64.gnu"
set +e
"$tempdir/tinycc_x87_uint64.gnu"
oracle_code=$?
set -e
test "$oracle_code" -eq 42
echo "PASS: GCC oracle x87 uint64->long double boundaries exit=42"

# GNU x87 return -> MiniC full unsigned integer conversion ABI.
# Test 2^63 and UINT64_MAX; FISTTP alone would sign-overflow.
cat > "$tempdir/tinycc_x87_fixuns_mini.i" <<'EOF'
extern long double gnu_x87_big(void);
extern long double gnu_x87_mid(void);
int main(void) {
    volatile long double big = gnu_x87_big();
    volatile long double mid = gnu_x87_mid();
    if ((unsigned long long)big != 0xffffffffffffffffULL) return 1;
    if ((unsigned long long)mid != 0x8000000000000000ULL) return 2;
    if ((unsigned int)mid != 0U) return 3;
    if ((long long)(long double)(-42LL) != -42LL) return 4;
    return 42;
}
EOF
cat > "$tempdir/tinycc_x87_fixuns_gnu.c" <<'EOF'
long double gnu_x87_big(void) { return (long double)0xffffffffffffffffULL; }
long double gnu_x87_mid(void) { return (long double)0x8000000000000000ULL; }
EOF
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/tinycc_x87_fixuns_mini.i" -o "$tempdir/tinycc_x87_fixuns_mini.s"
"$HOST_CC" -c -x assembler "$tempdir/tinycc_x87_fixuns_mini.s" -o "$tempdir/tinycc_x87_fixuns_mini.o"
"$HOST_CC" -c "$tempdir/tinycc_x87_fixuns_gnu.c" -o "$tempdir/tinycc_x87_fixuns_gnu.o"
"$HOST_CC" -no-pie "$tempdir/tinycc_x87_fixuns_mini.o" "$tempdir/tinycc_x87_fixuns_gnu.o" -o "$tempdir/tinycc_x87_fixuns.bin"
set +e
"$tempdir/tinycc_x87_fixuns.bin"
rc=$?
set -e
if [ "$rc" -ne 42 ]; then
  echo "FAIL: x87 GCC/MiniC unsigned-fix integer ABI actual=$rc expected=42" >&2
  exit 1
fi
echo "PASS: x86-64 GCC x87 -> MiniC uint64/uint32 and signed integer conversions exit=42"

# x86-64 uses the x87 80-bit ABI for real long double. A GNU-built
# callee returning st(0) and a MiniC-built caller must agree, including
# values between adjacent binary64 numbers.
cat > "$tempdir/tinycc_x87_return.i" <<'EOF'
extern long double gnu_x87_return(void);
int main(void) {
    long double value = gnu_x87_return();
    double n = (double)value;
    return n == 1.0 ? 42 : 1;
}
EOF
cat > "$tempdir/tinycc_x87_return.c" <<'EOF'
long double gnu_x87_return(void) { return 1.0L + 0x1p-60L; }
EOF
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/tinycc_x87_return.i" -o "$tempdir/tinycc_x87_return.s"
"$HOST_CC" -c -x assembler "$tempdir/tinycc_x87_return.s" -o "$tempdir/tinycc_x87_return.o"
"$HOST_CC" -c "$tempdir/tinycc_x87_return.c" -o "$tempdir/tinycc_x87_return.gnu.o"
"$HOST_CC" -no-pie "$tempdir/tinycc_x87_return.o" "$tempdir/tinycc_x87_return.gnu.o" -o "$tempdir/tinycc_x87_return.bin"
set +e
"$tempdir/tinycc_x87_return.bin"
native_result=$?
set -e
test "$native_result" -eq 42
echo "PASS: x86-64 x87 GNU callee / MiniC caller return ABI"

# Include the odd-spilled-stack x87 ABI test in the existing native M1
# regression suite; no new workflow or independently-built compiler.
sh tests/compiler/c0/run-x86_64-long-double-stack-abi.sh

# Explicit double -> long double -> double conversions must use
# x87, not the binary128 software conversion symbols.
cat > "$tempdir/tinycc_x87_convert.i" <<'EOF'
int main(void) {
    volatile double a = 4.75;
    long double b = (long double)a;
    volatile long double *p = &b;
    double c = (double)*p;
    return c == 4.75 ? 42 : 1;
}
EOF
check_exit tinycc_x87_convert 42

# TinyCC x86_64-gen.c needs dynamic Core stack allocation.
# Check a nonconstant size, lifetime across an 8-argument SysV call,
# and correct byte addressing without hardcoded final stack size.
cat > "$tempdir/tinycc_dynamic_alloca.i" <<'EOF'
static int sum8(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a+b+c+d+e+f+g+h;
}
static int check_dynamic(int n) {
    unsigned char *p = (unsigned char *)__builtin_alloca((unsigned long)n);
    p[0] = 3;
    p[n-1] = 3;
    return sum8(1,2,3,4,5,6,7,8) + p[0] + p[n-1];
}
int main(void) { return check_dynamic(97); }
EOF
check_exit tinycc_dynamic_alloca 42

# A file-scope record definition may include an object or pointer
# declarator; skipping directly to ';' loses the declarator and
# incorrectly rejects the valid syntax used by TinyCC tccgen.c.
cat > "$tempdir/tinycc_record_declarator.i" <<'EOF'
struct switch_t {
    struct case_t { long long value; int sym; } **p;
    int n;
} *cur_switch;
struct simple_t { int x; } global_simple;
struct forward_t;
struct forward_t { int y; };
int main(void) {
    static struct switch_t state;
    if (cur_switch != 0 || global_simple.x != 0) return 1;
    cur_switch = &state;
    cur_switch->n = 40;
    global_simple.x = 2;
    return cur_switch->n + global_simple.x;
}
EOF
check_exit tinycc_record_declarator 42

# BusyBox 1.36.1 pulls glibc's regexec prototype into findutils/grep.c:
# regmatch_t matches[__restrict nmatch] is an adjusted pointer parameter.
# It must retain preceding integer-parameter visibility in both prototypes
# and a real function definition (and must not become an array ABI).
cat > "$tempdir/busybox_regex_vla_parameter.i" <<'EOF'
typedef unsigned long size_t;
typedef struct { int so; int eo; } regmatch_t;
extern int check(size_t nmatch, regmatch_t matches[__restrict nmatch]);
int check(size_t nmatch, regmatch_t matches[__restrict nmatch]) {
    return matches[0].so + (int)nmatch;
}
int main(void) {
    regmatch_t matches[2] = {{40,0},{0,0}};
    return check(2, matches);
}
EOF
check_exit busybox_regex_vla_parameter 42

# A spelling with no preceding parameter is not a valid bound. Do not
# treat any identifier as an ignored array bound.
cat > "$tempdir/busybox_regex_unknown_bound.i" <<'EOF'
int bad(int values[undeclared_bound]);
int main(void) { return 0; }
EOF
if "$MINIC_CC" --target=x86_64-linux -S     "$tempdir/busybox_regex_unknown_bound.i" -o "$tempdir/busybox_regex_unknown_bound.s"     >"$tempdir/busybox_regex_unknown_bound.out" 2>&1; then
    echo "FAIL: unknown C99 array-parameter bound accepted" >&2
    exit 1
fi
echo "PASS: reject unknown array-parameter bound"

# BusyBox grep.c uses a complete GNU attributed record declaration
# without a global object. Such declarations must not be routed to
# the function/object declarator parser.
cat > "$tempdir/busybox_grep_record_type_attribute.i" <<'EOF'
struct grep_state { int a; unsigned char flag; } __attribute__((__may_alias__));
struct packed_state { char ch; int number; } __attribute__((packed));
struct extra { int value; } __attribute__((__may_alias__)) *extra_ptr;
int main(void) {
    struct grep_state state = {40, 2};
    struct packed_state packed = {1, 4};
    if (sizeof(packed) != 5 || extra_ptr != 0) return 1;
    return state.a + state.flag;
}
EOF
check_exit busybox_grep_record_type_attribute 42

# TinyCC tccgen.c declares an array and two pointers in the same
# tentative file-scope definition. This is standard C, not a workaround.
cat > "$tempdir/tinycc_mixed_tentative.i" <<'EOF'
typedef struct SValue { long bits; unsigned int flags; } SValue;
SValue __vstack[1+256], *vtop, *pvtop;
int main(void) {
    if (__vstack[0].bits != 0 || vtop != 0 || pvtop != 0) return 1;
    vtop = __vstack + 1;
    pvtop = __vstack + 2;
    vtop->bits = 21;
    pvtop->bits = 21;
    return vtop->bits + pvtop->bits;
}
EOF
check_exit tinycc_mixed_tentative 42

# GCC assert() expands __PRETTY_FUNCTION__ inside C functions; in C
# it denotes the lexical function name (unlike the C++ signature form).
cat > "$tempdir/tinycc_pretty_function.i" <<'EOF'
static int check_name(void) {
    const char *name = __PRETTY_FUNCTION__;
    return sizeof(__PRETTY_FUNCTION__) == 11u &&
           name[0] == 'c' && name[9] == 'e';
}
int main(void) { return check_name() ? 42 : 1; }
EOF
check_exit tinycc_pretty_function 42

# SQLite sqlite3AddInt64 uses checked builtin overflow with a typed
# output pointer. Verify stored wrapped bits and overflow flag independently.
cat > "$tempdir/sqlite_overflow.i" <<'EOF'
int main(void) {
    long long s;
    unsigned long long u;
    unsigned int w;
    if (!__builtin_add_overflow(9223372036854775807LL, 1LL, &s) ||
        s != (-9223372036854775807LL - 1LL)) return 1;
    if (!__builtin_sub_overflow((-9223372036854775807LL - 1LL), 1LL, &s) ||
        s != 9223372036854775807LL) return 2;
    if (!__builtin_mul_overflow(9223372036854775807LL, 2LL, &s) ||
        s != -2LL) return 3;
    if (__builtin_add_overflow(17LL, 25LL, &s) || s != 42LL) return 4;
    if (!__builtin_add_overflow(18446744073709551615ULL, 1ULL, &u) || u != 0ULL)
        return 5;
    if (!__builtin_sub_overflow(0ULL, 1ULL, &u) ||
        u != 18446744073709551615ULL) return 6;
    if (!__builtin_mul_overflow(18446744073709551615ULL, 2ULL, &u) ||
        u != 18446744073709551614ULL) return 7;
    if (__builtin_mul_overflow(6u, 7u, &w) || w != 42u) return 8;
    return 42;
}
EOF
check_exit sqlite_overflow 42

# SQLite sqlite3LogEst: x86 baseline BSR/BSF must preserve operand widths.
# Test genuine GNU bit-count builtins instead of rewriting source code.
cat > "$tempdir/sqlite_bit_count.i" <<'EOF'
int main(void) {
    unsigned int a = 1u;
    unsigned long b = 1ul;
    unsigned long long c = 1ULL << 63;
    if (__builtin_clz(a) != 31) return 1;
    if (__builtin_clzl(b) != 63) return 2;
    if (__builtin_clzll(c) != 0) return 3;
    if (__builtin_ctz(16u) != 4) return 4;
    if (__builtin_ctzl(1ul << 40) != 40) return 5;
    if (__builtin_ctzll(c) != 63) return 6;
    if (__builtin_clz(1u << 31) != 0) return 7;
    return 42;
}
EOF
check_exit sqlite_bit_count 42

# SQLite's sqlite3Parser(void*, int, Token) ABI: Token is an
# INTEGER/INTEGER two-eightbyte record in RDX/RCX after two scalar GPR args.
cat > "$tempdir/sqlite_token_by_value.i" <<'EOF'
typedef struct { const char *z; unsigned int n; } Token;
static int consume(void *opaque, int id, Token t) {
    int *out = (int *)opaque;
    *out = id + (int)t.n + (int)t.z[0];
    return *out;
}
int main(void) {
    int out = 0;
    Token t = {"x", 2};
    return consume(&out, 10, t) == 132 && out == 132 ? 42 : 1;
}
EOF
check_exit sqlite_token_by_value 42

# Six scalar GPRs leave no register pair for a following Token.
# The whole aggregate must be stack-passed, not split with a spare GPR.
cat > "$tempdir/sqlite_token_stack.i" <<'EOF'
typedef struct { const char *z; unsigned int n; } Token;
static int check(int a, int b, int c, int d, int e, int f, Token t) {
    return a+b+c+d+e+f + t.n + t.z[0];
}
int main(void) {
    Token token = {"x", 2};
    return check(1,2,3,4,5,6,token) == 143 ? 42 : 1;
}
EOF
check_exit sqlite_token_stack 42

# Cross-compiler oracle 1: GNU caller -> MiniC callee (incoming record).
cat > "$tempdir/sqlite_token_receive.i" <<'EOF'
typedef struct { const char *z; unsigned int n; } Token;
int minic_receiver(void *context, int id, Token t) {
    *(int *)context = id + t.n + t.z[0];
    return 42;
}
EOF
cat > "$tempdir/sqlite_token_host.c" <<'EOF'
typedef struct { const char *z; unsigned int n; } Token;
extern int minic_receiver(void *, int, Token);
int main(void) {
    int state = 0;
    Token token = {"x", 2};
    int code = minic_receiver(&state, 10, token);
    return code == 42 && state == 132 ? 42 : 1;
}
EOF
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/sqlite_token_receive.i" \
    -o "$tempdir/sqlite_token_receive.s"
"$HOST_CC" -c -x assembler "$tempdir/sqlite_token_receive.s" -o "$tempdir/sqlite_token_receive.o"
"$HOST_CC" -no-pie "$tempdir/sqlite_token_host.c" "$tempdir/sqlite_token_receive.o" \
    -o "$tempdir/sqlite_token_receive.bin"
set +e
"$tempdir/sqlite_token_receive.bin"
result=$?
set -e
test "$result" -eq 42
echo "PASS: x86-64 Token ABI GNU caller / MiniC callee exit=42"

# Cross-compiler oracle 2: MiniC caller -> GNU callee (outgoing record).
cat > "$tempdir/sqlite_token_send.i" <<'EOF'
typedef struct { const char *z; unsigned int n; } Token;
extern int gcc_receiver(void *, int, Token);
int main(void) {
    Token token = {"x", 2};
    int state = 0;
    int code = gcc_receiver(&state, 10, token);
    return code == 42 && state == 132 ? 42 : 1;
}
EOF
cat > "$tempdir/sqlite_token_receiver.c" <<'EOF'
typedef struct { const char *z; unsigned int n; } Token;
int gcc_receiver(void *context, int id, Token t) {
    *(int *)context = id + t.n + t.z[0];
    return 42;
}
EOF
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/sqlite_token_send.i" \
    -o "$tempdir/sqlite_token_send.s"
"$HOST_CC" -c -x assembler "$tempdir/sqlite_token_send.s" -o "$tempdir/sqlite_token_send.o"
"$HOST_CC" -no-pie "$tempdir/sqlite_token_receiver.c" "$tempdir/sqlite_token_send.o" \
    -o "$tempdir/sqlite_token_send.bin"
set +e
"$tempdir/sqlite_token_send.bin"
result=$?
set -e
test "$result" -eq 42
echo "PASS: x86-64 Token ABI MiniC caller / GNU callee exit=42"

cat > "$tempdir/branch.i" <<'EOF'
int main(void) {
    int a = 43;
    if (a > 20) return a - 1;
    return 0;
}
EOF
check_exit branch 42

cat > "$tempdir/locals.i" <<'EOF'
int main(void) {
    int x = 21;
    int y = x + x;
    return y;
}
EOF
check_exit locals 42

cat > "$tempdir/mixed_qualifiers.i" <<'EOF'
int main(void) {
    unsigned const char a = 20;
    long volatile int b = 22;
    unsigned char const *q = &a;
    return (int)*q + (int)b;
}
EOF
check_exit mixed_qualifiers 42


# SQLite 3.54.1 first atomic builtin: real x86 naturally aligned
# relaxed stores, including the single-evaluation expression case.
cat > "$tempdir/sqlite_atomic_store.i" <<'EOF'
unsigned int global_atomic;
unsigned long long wide_atomic;
volatile unsigned short volatile_atomic;
int main(void) {
    unsigned int *p = &global_atomic;
    int side = 0;
    __atomic_store_n(p, 0x12345678u, 0);
    if (global_atomic != 0x12345678u) return 1;
    __atomic_store_n(&wide_atomic, 0x123456789abcdef0ULL, 0);
    if (wide_atomic != 0x123456789abcdef0ULL) return 2;
    __atomic_store_n(&volatile_atomic, 0x3412u, 0);
    if (volatile_atomic != 0x3412u) return 3;
    __atomic_store_n(p, ++side, 0);
    if (side != 1 || global_atomic != 1u) return 4;
    return 42;
}
EOF
check_exit sqlite_atomic_store 42
cat > "$tempdir/sqlite_atomic_store_seqcst.i" <<'EOF'
unsigned int state;
int main(void) { __atomic_store_n(&state, 42u, 5); return 42; }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/sqlite_atomic_store_seqcst.i" \
    -o "$tempdir/sqlite_atomic_store_seqcst.s" \
    >"$tempdir/sqlite_atomic_store_seqcst.stdout" 2>"$tempdir/sqlite_atomic_store_seqcst.stderr"; then
    echo "FAIL: x86 atomic seq_cst incorrectly accepted as relaxed MOV" >&2
    exit 1
fi
test ! -e "$tempdir/sqlite_atomic_store_seqcst.s"
grep -F 'only supports relaxed memory order' "$tempdir/sqlite_atomic_store_seqcst.stderr" >/dev/null
echo "PASS: x86 relaxed atomic stores 2/4/8 byte; seq_cst fail-closed"
# SQLite pairs its relaxed write with a relaxed read. Verify the exact
# value, a volatile source and one-evaluation of the address expression.
cat > "$tempdir/sqlite_atomic_load.i" <<'EOF'
unsigned int state;
volatile unsigned long long wide_state;
int main(void) {
    unsigned int *addresses[2] = {&state, &state};
    int offset = 0;
    __atomic_store_n(&state, 0x31415926u, 0);
    if (__atomic_load_n(addresses[offset++], 0) != 0x31415926u || offset != 1) return 1;
    __atomic_store_n(&wide_state, 0x0123456789abcdefULL, 0);
    if (__atomic_load_n(&wide_state, 0) != 0x0123456789abcdefULL) return 2;
    return 42;
}
EOF
check_exit sqlite_atomic_load 42
cat > "$tempdir/sqlite_atomic_load_seqcst.i" <<'EOF'
unsigned int state;
int main(void) { return __atomic_load_n(&state, 5); }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/sqlite_atomic_load_seqcst.i" \
    -o "$tempdir/sqlite_atomic_load_seqcst.s" \
    >"$tempdir/sqlite_atomic_load_seqcst.out" 2>"$tempdir/sqlite_atomic_load_seqcst.err"; then
    echo "FAIL: seq_cst read unexpectedly accepted as relaxed" >&2
    exit 1
fi
test ! -e "$tempdir/sqlite_atomic_load_seqcst.s"
grep -F 'only supports relaxed memory order' "$tempdir/sqlite_atomic_load_seqcst.err" >/dev/null
echo "PASS: x86 relaxed atomic loads 4/8 byte; stronger order fail-closed"

# SQLite nearlyFull uses short-circuit && as the source value.  It
# must not leave atomic.store referring to SSA values from predecessor blocks.
cat > "$tempdir/sqlite_atomic_short_circuit.i" <<'EOF'
volatile unsigned int nearlyFull;
int main(void) {
    unsigned int n = 3, used = 4;
    unsigned int offset = 0, cells[2] = {0, 0};
    __atomic_store_n(&nearlyFull, (n > 0 && n <= used), 0);
    if (__atomic_load_n(&nearlyFull, 0) != 1u) return 1;
    __atomic_store_n(&cells[offset++], (n == 0 || used > 3), 0);
    if (offset != 1 || cells[0] != 1u || cells[1] != 0u) return 2;
    return 42;
}
EOF
check_exit sqlite_atomic_short_circuit 42

# SQLite sqlite3_config atomically publishes a callback and its context.
# Exercise both object and function pointer relaxed operations.
cat > "$tempdir/sqlite_atomic_pointer.i" <<'EOF'
typedef void (*LogFn)(void *, int, const char *);
static void logger(void *context, int code, const char *message) {
    if (message[0] == 'x') *(int *)context = code + 20;
}
LogFn xLog;
void *pLogArg;
void *volatile volatileContext;
int main(void) {
    int state = 0;
    LogFn callback = logger;
    void *context = &state;
    LogFn loaded;
    __atomic_store_n(&xLog, callback, 0);
    __atomic_store_n(&pLogArg, context, 0);
    __atomic_store_n(&volatileContext, context, 0);
    if (__atomic_load_n(&xLog, 0) != callback) return 1;
    if (__atomic_load_n(&pLogArg, 0) != context) return 2;
    if (__atomic_load_n(&volatileContext, 0) != context) return 3;
    loaded = __atomic_load_n(&xLog, 0);
    loaded(__atomic_load_n(&pLogArg, 0), 22, "x");
    return state;
}
EOF
check_exit sqlite_atomic_pointer 42

# Explicit -fPIE: dynamic libc function *and object* address materialization
# must resolve via GOT, whereas the original kernel's -fno-PIE permits PC32.
cat > "$tempdir/pie_external_symbols.i" <<'EOF'
extern char *getenv(const char *);
extern char **environ;
char *(*lookup(void))(const char *) { return getenv; }
char ***environment_location(void) { return &environ; }
int main(void) {
    char *(*fn)(const char *) = lookup();
    char ***envp = environment_location();
    return fn("PATH") != 0 && *envp != 0 ? 42 : 1;
}
EOF
"$MINIC_CC" --target=x86_64-linux -fPIE -S "$tempdir/pie_external_symbols.i" \
    -o "$tempdir/pie_external_symbols.s"
grep -F '@GOTPCREL(%rip)' "$tempdir/pie_external_symbols.s" >/dev/null
"$HOST_CC" -pie -x assembler "$tempdir/pie_external_symbols.s" \
    -o "$tempdir/pie_external_symbols.bin"
set +e
"$tempdir/pie_external_symbols.bin"
pie_code=$?
set -e
if test "$pie_code" -ne 42; then
  echo "FAIL: default PIE external function/global symbols exit=$pie_code expected=42" >&2
  exit 1
fi
# Keep the previous non-PIE environment supported by the same machine code.
"$HOST_CC" -no-pie -x assembler "$tempdir/pie_external_symbols.s" \
    -o "$tempdir/nonpie_external_symbols.bin"
set +e
"$tempdir/nonpie_external_symbols.bin"
nonpie_code=$?
set -e
if test "$nonpie_code" -ne 42; then
  echo "FAIL: non-PIE external function/global symbols exit=$nonpie_code expected=42" >&2
  exit 1
fi
echo "PASS: x86-64 GOT external function/object addresses PIE and non-PIE exit=42"
# The GNU kernel Kbuild explicitly requests -fno-PIE. Keep default-visible
# addresses PC-relative under that mode, and prove GNU -no-pie static link.
"$MINIC_CC" --target=x86_64-linux -fno-PIE -S "$tempdir/pie_external_symbols.i" \
    -o "$tempdir/nopic_external_symbols.s"
grep -F 'leaq getenv(%rip)' "$tempdir/nopic_external_symbols.s" >/dev/null
grep -F 'leaq environ(%rip)' "$tempdir/nopic_external_symbols.s" >/dev/null
if grep -F '@GOTPCREL' "$tempdir/nopic_external_symbols.s" >/dev/null;then
  echo "FAIL: -fno-PIE x86 external addresses unexpectedly use GOT" >&2
  exit 1
fi
"$HOST_CC" -no-pie -x assembler "$tempdir/nopic_external_symbols.s" \
    -o "$tempdir/nopic_external_symbols.bin"
set +e
"$tempdir/nopic_external_symbols.bin"
nopic_code=$?
set -e
if test "$nopic_code" -ne 42; then
  echo "FAIL: -fno-PIE external function/global addresses exit=$nopic_code expected=42" >&2
  exit 1
fi
echo "PASS: x86-64 non-PIC direct function/object addresses GNU no-pie exit=42"

# Real tiny-AES pressure point: read-only global byte tables.
cat > "$tempdir/global.i" <<'EOF'
static const unsigned char table[2] = {10, 32};
int main(void) { return table[0] + table[1]; }
EOF
check_exit global 42

# Lua 5.5.0 pressure point: string/global addresses may be encoded as
# zero-filled pointer objects with one relocation and *no* literal slot.
cat > "$tempdir/lua_pointer_globals.i" <<'EOF'
static const char text[] = "Lua";
static const char *progname = "lua";
static const char *const hook_key = text;
int main(void) {
    return progname[0] == 'l' && progname[2] == 'a' &&
           hook_key[0] == 'L' && hook_key[2] == 'a' ? 42 : 0;
}
EOF
check_exit lua_pointer_globals 42

# cJSON pressure point: record layout and function pointer relocations.
cat > "$tempdir/record.i" <<'EOF'
struct Pair { int a; int b; };
static struct Pair value = {20, 22};
int main(void) { return value.a + value.b; }
EOF
check_exit record 42

cat > "$tempdir/function_reloc.i" <<'EOF'
static int identity(int value) { return value; }
struct Hook { int (*function)(int); int number; };
static struct Hook hook = {identity, 42};
int main(void) {
    if (hook.function == 0) return 0;
    return hook.number;
}
EOF
check_exit function_reloc 42
# zlib configuration_table uses an array of records with function relocations.
cat > "$tempdir/record_array_reloc.i" <<'EOF'
typedef unsigned short ush;
static int add(int a, int b) { return a + b; }
struct Config { ush good, lazy, nice, chain; int (*func)(int, int); };
static const struct Config configurations[2] = {
    {2, 3, 4, 5, add}, {10, 1, 2, 3, add}
};
int main(void) {
    if (configurations[0].func(20, 22) != 42) return 1;
    if (configurations[0].good + configurations[1].good != 12) return 2;
    return 42;
}
EOF
check_exit record_array_reloc 42

cat > "$tempdir/pointer_array_reloc.i" <<'EOF'
static const char *const z_errmsg[2] = { "A", "B" };
int main(void) {
    return z_errmsg[0][0] + z_errmsg[1][0] - 89;
}
EOF
check_exit pointer_array_reloc 42

# SQLite ExprList_item zeroItem = {0}; requires bytewise zero for
# a full record despite bit-fields and flattened logical initializer slots.
cat > "$tempdir/zero_record_bitfields.i" <<'EOF'
struct Nested { int items[3]; void *opaque; };
struct Entry {
    unsigned int flag:3;
    unsigned int mode:5;
    struct Nested nested;
    int final;
};
static const struct Entry zeroItem = {0};
int main(void) {
    return zeroItem.flag == 0 && zeroItem.mode == 0 &&
           zeroItem.nested.items[0] == 0 &&
           zeroItem.nested.items[2] == 0 &&
           zeroItem.nested.opaque == 0 && zeroItem.final == 0 ? 42 : 1;
}
EOF
check_exit zero_record_bitfields 42

# SQLite date.c's aXformType uses inline char[] fields inside an
# array of records. Verify the same flattened slots and ABI data layout.
cat > "$tempdir/record_array_embedded_chars.i" <<'EOF'
typedef unsigned char u8;
static const struct {
    u8 kind, len;
    char text[6];
    double limit, factor;
} transforms[] = {
    {0, 6, {'s','e','c','o','n','d'}, 42.0, 1000.0},
    {1, 6, {'m','i','n','u','t','e'}, 20.0, 22.0}
};
int main(void) {
    if (transforms[0].text[0] != 's' || transforms[1].text[5] != 'e')
        return 1;
    return transforms[0].limit == 42.0 &&
           transforms[1].limit + transforms[1].factor == 42.0 ? 42 : 2;
}
EOF
check_exit record_array_embedded_chars 42

# Lua lvm.c pressure: static dispatch tables initialized from GNU
# function-local labels. Validate relocations against real emitted labels.
cat > "$tempdir/lua_label_table.i" <<'EOF'
int main(void) {
    static void *const dispatch[2] = {&&label42, &&other};
    if (dispatch[0] == 0 || dispatch[1] == 0) return 1;
    goto *dispatch[0];
other:
    return 0;
label42:
    return 42;
}
EOF
check_exit lua_label_table 42

# Lua direct records with selected union members; unlike zlib's existing
# array-of-records fixture, these objects themselves have record type.
cat > "$tempdir/lua_recursive_record.i" <<'EOF'
typedef union { unsigned short freq; unsigned short code; } U;
typedef struct { int key; U one; U two; int value; } R;
static const U nativeendian = { 42 };
static const R table = { 20, {10}, {12}, 0 };
int main(void) {
    if (nativeendian.freq != 42) return 1;
    return table.key + table.one.freq + table.two.code + table.value;
}
EOF
check_exit lua_recursive_record 42

# zlib trees.h static_ltree: array of records with nested selected unions.
cat > "$tempdir/union_record_array.i" <<'EOF'
typedef unsigned short ush;
typedef struct {
    union { ush freq; ush code; } fc;
    union { ush dad; ush len; } dl;
} ct_data;
static const ct_data tree[2] = {
    {{20},{22}}, {{1},{2}}
};
int main(void) { return tree[0].fc.freq + tree[0].dl.dad; }
EOF
check_exit union_record_array 42






cat > "$tempdir/function_address.i" <<'EOF'
static int identity(int value) { return value; }
int main(void) {
    int (*function)(int) = identity;
    return function == 0 ? 0 : 42;
}
EOF
check_exit function_address 42

# cJSON frontier: Core RECORD_COPY transfers an aggregate's entire byte image.
cat > "$tempdir/record_copy.i" <<'EOF'
struct Item { int a; int b; int c; };
int main(void) {
    struct Item first;
    struct Item second;
    first.a = 20;
    first.b = 22;
    first.c = 7;
    second = first;
    first.b = 3;
    if (second.c != 7) return 1;
    return second.a + second.b;
}
EOF
check_exit record_copy 42

# cJSON's allocator hooks require a real call through a function pointer.
cat > "$tempdir/indirect_call.i" <<'EOF'
static int add(int x, int y) { return x + y; }
struct Hooks { int (*calculate)(int, int); };
static struct Hooks hooks = { add };
int main(void) { return hooks.calculate(20, 22); }
EOF
check_exit indirect_call 42

# zlib deflateInit_(...) forwards eight scalar arguments: two are on stack.
cat > "$tempdir/stack_args.i" <<'EOF'
int sum8(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}
int main(void) { return sum8(1,2,3,4,5,6,7,8) + 6; }
EOF
check_exit stack_args 42

cat > "$tempdir/stack_indirect.i" <<'EOF'
static int eight(int a,int b,int c,int d,int e,int f,int g,int h) {
    return g + h;
}
int main(void) {
    int (*p)(int,int,int,int,int,int,int,int) = eight;
    return p(1,2,3,4,5,6,20,22);
}
EOF
check_exit stack_indirect 42

cat > "$tempdir/stack_double.i" <<'EOF'
double ten(double a, double b, double c, double d, double e,
           double f, double g, double h, double i, double j) {
    return i + j;
}
int main(void) {
    return ten(0,0,0,0,0,0,0,0,20.5,21.5) == 42.0 ? 42 : 0;
}
EOF
check_exit stack_double 42

cat > "$tempdir/stack_mixed.i" <<'EOF'
int mixed(int a, int b, int c, int d, int e, int f, int g,
          double d0,double d1,double d2,double d3,double d4,
          double d5,double d6,double d7,double d8, int h) {
    return g + h + (int)d8;
}
int main(void) {
    return mixed(1,2,3,4,5,6,10,0,0,0,0,0,0,0,0,20.0,12);
}
EOF
check_exit stack_mixed 42

# The x86 System V va_list is a mutable 24-byte tag, not RV64's stack
# cursor. Verify native glibc can consume the GPR/XMM register save area,
# overflow stack arguments, and an independent va_start cursor.
cat > "$tempdir/varargs_forward.i" <<'EOF'
typedef __builtin_va_list va_list;
extern int vsnprintf(char *restrict, unsigned long, const char *restrict, va_list);
extern int strcmp(const char *, const char *);

static int format(char *buffer, const char *fmt, ...) {
    va_list args;
    int len;
    __builtin_va_start(args, fmt);
    len = vsnprintf(buffer, 200, fmt, args);
    __builtin_va_end(args);
    return len;
}
int main(void) {
    char buf[200];
    int n = format(buf, "%d:%d:%d:%d:%d:%d:%d|%.1f",
                   1,2,3,4,5,6,7,8.5);
    if (n != 17 || strcmp(buf, "1:2:3:4:5:6:7|8.5") != 0) return 1;
    n = format(buf, "%.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f",
               1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0);
    return n == 17 && strcmp(buf, "1 2 3 4 5 6 7 8 9") == 0 ? 42 : 2;
}
EOF
check_exit varargs_forward 42

cat > "$tempdir/varargs_read.i" <<'EOF'
typedef __builtin_va_list va_list;
static int sum_mixed(void *opaque, ...) {
    va_list args;
    int i;
    int int_sum = 0;
    double fp_sum = 0.0;
    __builtin_va_start(args, opaque);
    for (i = 0; i < 7; ++i)
        int_sum += __builtin_va_arg(args, int);
    for (i = 0; i < 9; ++i)
        fp_sum += __builtin_va_arg(args, double);
    __builtin_va_end(args);
    return int_sum + (int)fp_sum - 31;
}
int main(void) {
    return sum_mixed((void *)0, 1,2,3,4,5,6,7,
                     1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0);
}
EOF
check_exit varargs_read 42

# The source SysV va_list is an array of one 24-byte record, independently
# from the backend's private pointer-to-state cursor transport. Both a
# typedef sizeof and a local object sizeof must agree with native GCC.
cat > "$tempdir/varargs_sizeof.i" <<'EOF'
typedef __builtin_va_list va_list;
int main(void) {
    va_list list;
    return sizeof(va_list) == 24U && sizeof(list) == 24U ? 42 : 13;
}
EOF
check_exit varargs_sizeof 42
"$HOST_CC" -x c -std=gnu11 "$tempdir/varargs_sizeof.i" -o "$tempdir/gcc-va-sizeof"
set +e
"$tempdir/gcc-va-sizeof"
gcc_sizeof_status=$?
set -e
test "$gcc_sizeof_status" -eq 42

cat > "$tempdir/varargs_type_identity.i" <<'EOF'
typedef __builtin_va_list va_list;
_Static_assert(!__builtin_types_compatible_p(va_list, void *),
               "x86 SysV va_list has distinct type identity");
int main(void) { return 42; }
EOF
check_exit varargs_type_identity 42
"$HOST_CC" -x c -std=gnu11 -c -o "$tempdir/gcc-va-type.o" \
    "$tempdir/varargs_type_identity.i"
echo "PASS: x86 SysV va_list source array identity and GCC-matched 24-byte sizeof"

# A SysV va_copy must clone its 24-byte tag. Check independently advancing
# both cursors, rather than only checking that parsing/code generation succeeds.
cat > "$tempdir/varargs_copy.i" <<'EOF'
typedef __builtin_va_list va_list;
int verify_copy(int fixed, ...) {
    va_list first, second;
    long a, b, next_a, next_b;
    __builtin_va_start(first, fixed);
    __builtin_va_copy(second, first);
    a = __builtin_va_arg(first, long);
    next_a = __builtin_va_arg(first, long);
    b = __builtin_va_arg(second, long);
    next_b = __builtin_va_arg(second, long);
    __builtin_va_end(second);
    __builtin_va_end(first);
    return fixed == 7 && a == 17 && next_a == 19 &&
           b == 17 && next_b == 19;
}
int main(void) { return verify_copy(7, 17L, 19L) ? 42 : 13; }
EOF
check_exit varargs_copy 42
"$HOST_CC" -x c -std=gnu11 "$tempdir/varargs_copy.i" -o "$tempdir/gcc-varargs-copy"
set +e
"$tempdir/gcc-varargs-copy"
gcc_copy_status=$?
set -e
test "$gcc_copy_status" -eq 42
echo "PASS: SysV x86 va_copy snapshots independent 24-byte ABI cursor"




# Original BusyBox SHA CPUID and TLS large-number multiply-accumulate use
# the same GNU extended-asm input/output constraint family. These exercise
# the unmodified source spellings: four matched physical outputs, =rm mixed
# register/memory outputs, explicit RAX/memory inputs and cc/dx clobbers.
# Run a genuine GCC executable as the runtime output oracle.
cat > "$tempdir/x86_asm_registers.c" <<'EOF'
#include <stdio.h>
#include <stdint.h>
static void cpuid4(unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{
    asm ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "0"(*a), "1"(*b), "2"(*c), "3"(*d));
}
static void carry3(unsigned long a, unsigned long b,
                   unsigned long low, unsigned long mid, unsigned long high)
{
    unsigned long aa[1] = {a};
    unsigned long bb[1] = {b};
    asm volatile (
"\n      mulq  %7"
"\n      addq  %%rax, %0"
"\n      adcq  %%rdx, %1"
"\n      adcq  $0, %2"
        : "=rm" (low), "=rm" (mid), "=rm" (high)
        : "0" (low), "1" (mid), "2" (high),
          "a" (aa[0]), "m" (bb[0])
        : "cc", "dx");
    printf("mulacc %016lx %016lx %016lx\n", low, mid, high);
}
int main(void)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    cpuid4(&a, &b, &c, &d);
    printf("cpuid0 %08x %08x %08x %08x\n", a, b, c, d);
    a = 7; b = 0; c = 0; d = 0;
    cpuid4(&a, &b, &c, &d);
    printf("cpuid7 %08x %08x %08x %08x\n", a, b, c, d);
    carry3(3UL, 5UL, ~0UL, 8UL, 9UL);
    carry3(~0UL, ~0UL, 11UL, 13UL, 17UL);
    return 42;
}
EOF
"$HOST_CC" -std=gnu11 -E -P -x c "$tempdir/x86_asm_registers.c" \
    -o "$tempdir/x86_asm_registers.i"
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/x86_asm_registers.i" \
    -o "$tempdir/x86_asm_registers.s"
"$HOST_CC" -no-pie -x assembler "$tempdir/x86_asm_registers.s" \
    -o "$tempdir/x86_asm_registers.mini"
"$HOST_CC" -no-pie -std=gnu11 -x c "$tempdir/x86_asm_registers.i" \
    -o "$tempdir/x86_asm_registers.gnu"
set +e
"$tempdir/x86_asm_registers.mini" > "$tempdir/x86_asm_registers.mini.out"
mini_status=$?
"$tempdir/x86_asm_registers.gnu" > "$tempdir/x86_asm_registers.gnu.out"
gnu_status=$?
set -e
test "$mini_status" -eq 42
test "$gnu_status" -eq 42
cmp "$tempdir/x86_asm_registers.gnu.out" "$tempdir/x86_asm_registers.mini.out"
echo "PASS: x86 fixed/matched-register CPUID + cc/dx-clobbered MULADC GNU executable byte-exact"

# A second x86 contract family shares output/matched input semantics but
# uses GNU "memory" rather than fixed registers. The real BusyBox TLS 256-bit
# add/sub source uses four "=r" outputs, three matching pointer inputs and a
# carry/borrow register; its P-256 modulus path has one or three outputs.
cat > "$tempdir/x86_asm_memory.c" <<'EOF'
#include <stdio.h>
static long add4(unsigned long *r, const unsigned long *a,
                 const unsigned long *b)
{
    unsigned long carry;
    asm volatile (
"\n movq (%0), %3"
"\n addq (%1), %3"
"\n movq %3, (%2)"
"\n movq 1*8(%0), %3"
"\n adcq 1*8(%1), %3"
"\n movq %3, 1*8(%2)"
"\n movq 2*8(%0), %3"
"\n adcq 2*8(%1), %3"
"\n movq %3, 2*8(%2)"
"\n movq 3*8(%0), %3"
"\n adcq 3*8(%1), %3"
"\n movq %3, 3*8(%2)"
"\n sbbq %3, %3"
      : "=r"(a), "=r"(b), "=r"(r), "=r"(carry)
      : "0"(a), "1"(b), "2"(r) : "memory");
    return carry;
}
static long sub4(unsigned long *r, const unsigned long *a,
                 const unsigned long *b)
{
    unsigned long borrow;
    asm volatile (
"\n movq (%0), %3"
"\n subq (%1), %3"
"\n movq %3, (%2)"
"\n movq 1*8(%0), %3"
"\n sbbq 1*8(%1), %3"
"\n movq %3, 1*8(%2)"
"\n movq 2*8(%0), %3"
"\n sbbq 2*8(%1), %3"
"\n movq %3, 2*8(%2)"
"\n movq 3*8(%0), %3"
"\n sbbq 3*8(%1), %3"
"\n movq %3, 3*8(%2)"
"\n sbbq %3, %3"
      : "=r"(a), "=r"(b), "=r"(r), "=r"(borrow)
      : "0"(a), "1"(b), "2"(r) : "memory");
    return borrow;
}
static void dec_mod(unsigned long *p)
{
    asm volatile ("subq $1, (%0)" : "=r"(p) : "0"(p) : "memory");
}
static long three_output(unsigned long *p)
{
    unsigned long offset, borrow;
    /* Original BusyBox 256-bit modulus subtraction: complement carry and
     * a matched 0xffffffff unsigned-int constant passed to a 64-bit register. */
    asm volatile (
"\n addq $1, (%0)"
"\n cmc"
"\n sbbq %1, 1*8(%0)"
"\n sbbq $0, 2*8(%0)"
"\n movq 3*8(%0), %2"
"\n sbbq $0, %2"
"\n addq %1, %2"
"\n movq %2, 3*8(%0)"
      : "=r"(p), "=r"(offset), "=r"(borrow)
      : "0"(p), "1"(0x00000000ffffffff) : "memory");
    return borrow;
}
int main(void)
{
    unsigned long a[4] = {~0UL, 0, 0, 0};
    unsigned long b[4] = {2, 0, 0, 0};
    unsigned long r[4] = {0, 0, 0, 0};
    long carry = add4(r,a,b);
    printf("add %ld %lu %lu %lu %lu\n",carry,r[0],r[1],r[2],r[3]);
    carry = sub4(r,a,b);
    printf("sub %ld %lu %lu %lu %lu\n",carry,r[0],r[1],r[2],r[3]);
    dec_mod(r);
    printf("dec %lu\n",r[0]);
    carry = three_output(r);
    printf("three %ld %lu\n",carry,r[0]);
    return 42;
}
EOF
"$HOST_CC" -std=gnu11 -E -P -x c "$tempdir/x86_asm_memory.c" \
    -o "$tempdir/x86_asm_memory.i"
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/x86_asm_memory.i" \
    -o "$tempdir/x86_asm_memory.s"
"$HOST_CC" -no-pie -x assembler "$tempdir/x86_asm_memory.s" \
    -o "$tempdir/x86_asm_memory.mini"
"$HOST_CC" -no-pie -std=gnu11 -x c "$tempdir/x86_asm_memory.i" \
    -o "$tempdir/x86_asm_memory.gnu"
set +e
"$tempdir/x86_asm_memory.mini" > "$tempdir/x86_asm_memory.mini.out"
mini_status=$?
"$tempdir/x86_asm_memory.gnu" > "$tempdir/x86_asm_memory.gnu.out"
gnu_status=$?
set -e
test "$mini_status" -eq 42
test "$gnu_status" -eq 42
cmp "$tempdir/x86_asm_memory.gnu.out" "$tempdir/x86_asm_memory.mini.out"
echo "PASS: x86 four/three/one output matched-register memory asm GNU byte-exact"

# The above does not grant arbitrary templates with similar constraints.
cat > "$tempdir/x86_asm_registers_unsupported.i" <<'EOF'
int main(void) {
    unsigned a = 7, b = 0, c = 0, d = 0;
    asm("ud2" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
              : "0"(a), "1"(b), "2"(c), "3"(d));
    return 0;
}
EOF
if "$MINIC_CC" --target=x86_64-linux -S \
    "$tempdir/x86_asm_registers_unsupported.i" \
    -o "$tempdir/x86_asm_registers_unsupported.s" > "$tempdir/x86asm.out" \
    2> "$tempdir/x86asm.err"; then
    echo "FAIL: unsupported x86 asm template accepted without register allocator" >&2
    exit 1
fi
echo "PASS: unsupported x86 extended asm remains fail-closed"

# SQLite 3.54.1 glibc <byteswap.h> uses these GNU builtins.
# Runtime fixture checks full width, zero, overflow and *single evaluation*.
cat > "$tempdir/sqlite_bswap.i" <<'EOF'
unsigned short a16(unsigned int x) { return __builtin_bswap16(x); }
unsigned int a32(unsigned int x) { return __builtin_bswap32(x); }
unsigned long long a64(unsigned long long x) { return __builtin_bswap64(x); }
int main(void) {
    unsigned int a[] = {0x12345678u,0xdeadbeefu};
    int side = 0;
    if (a16(0x1234u) != 0x3412u) return 1;
    if (a32(0x12345678u) != 0x78563412u) return 2;
    if (a64(0x0123456789abcdefULL) != 0xefcdab8967452301ULL) return 3;
    if (a16(0xffffu) != 0xffffu || a32(0u) != 0u) return 4;
    if (__builtin_bswap32(a[side++]) != 0x78563412u || side != 1) return 5;
    return 42;
}
EOF
check_exit sqlite_bswap 42

# SQLite 3.54.1 sqlite3Fp10Convert2() real upstream pressure:
# __builtin_inff must be a binary32 +infinity, not a double or finite max.
# Also retain previously supported binary64 __builtin_huge_val().
cat > "$tempdir/sqlite_infinity.i" <<'EOF'
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_inff()), float),
               "builtin_inff returns float");
_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_inf()), double),
               "builtin_inf returns double");
float f_inf(void) { return __builtin_inff(); }
double d_inf(void) { return __builtin_inf(); }
double d_huge(void) { return __builtin_huge_val(); }
int main(void) {
    volatile float f = f_inf();
    volatile double d = d_inf();
    if (!(f > 3.4028234e38f) || !(d > 1.7976931348623157e308)) return 1;
    if (f != f || d != d) return 2;
    if (!(d_huge() == d)) return 3;
    if ((double)f != d) return 4;
    return 42;
}
EOF
check_exit sqlite_infinity 42
"$HOST_CC" -std=gnu11 -x c -c "$tempdir/sqlite_infinity.i" -o "$tempdir/sqlite_infinity-gcc.o"
echo "PASS: x86 SQLite builtin_inff/inf/huge_val type, infinity, and float ABI"

# Floating values use raw bits in Core slots; comparisons must reject NaNs.
cat > "$tempdir/double_core.i" <<'EOF'
int main(void) {
    double a = 1.5;
    double b = 2.5;
    double c = a + b;
    double invalid = 0.0 / 0.0;
    if (c != 4.0) return 1;
    if (invalid == invalid) return 2;
    return 42;
}
EOF
check_exit double_core 42

# x86-64 SysV FP arguments/results travel in xmm0-7, not integer registers.
cat > "$tempdir/double_abi.i" <<'EOF'
double add(double left, double right) {
    return left + right;
}
int main(void) {
    double result = add(20.5, 21.5);
    return result == 42.0 ? 42 : 0;
}
EOF
check_exit double_abi 42

cat > "$tempdir/int_to_double.i" <<'EOF'
int main(void) {
    int signed_value = -8;
    unsigned long large = 18446744073709551615UL;
    double a = (double)signed_value + 50.0;
    double b = (double)large;
    if (b < 18446744073709551616.0) return 1;
    return a == 42.0 ? 42 : 0;
}
EOF
check_exit int_to_double 42

cat > "$tempdir/double_to_int.i" <<'EOF'
int main(void) {
    double a = 44.75;
    double b = -2.5;
    unsigned long upper = (unsigned long)9223372036854775808.0;
    if (upper != 9223372036854775808UL) return 1;
    return (int)a + (int)b;
}
EOF
check_exit double_to_int 42

cat > "$tempdir/float_to_double.i" <<'EOF'
int main(void) {
    float a = 1.25f;
    float b = 2.5f;
    float sum = a + b;
    double d = (double)sum;
    float narrowed = (float)(d + 38.25);
    return (double)narrowed == 42.0 ? 42 : 0;
}
EOF
check_exit float_to_double 42








# Real-project-driven reuse of recursive top-level record initialization:
# nested records and embedded arrays must retain target DataLayout padding.
cat > "$tempdir/nested_record.i" <<'EOF'
struct Pair { int a; int b; };
struct Nested { struct Pair pair; int c; };
static struct Nested value = {{20, 22}, 1};
int main(void) {
    return value.pair.a + value.pair.b == 42 && value.c == 1 ? 42 : 1;
}
EOF
check_exit nested_record 42
cat > "$tempdir/nested_record_arrays.i" <<'EOF'
struct Pair { int a; int b; };
struct Mixed { int prefix; char label[4]; struct Pair pair; int suffix[2]; };
static struct Mixed value = {7, {'a', 'b', 0, 0}, {15, 20}, {3, 4}};
int main(void) {
    if (value.label[0] != 'a' || value.label[1] != 'b') return 1;
    return value.prefix + value.pair.a + value.pair.b == 42 &&
           value.suffix[0] == 3 && value.suffix[1] == 4 ? 42 : 2;
}
EOF
check_exit nested_record_arrays 42

# GNU statement-expression result and effect forms are separately admitted by
# Core CFG lowering. One runtime checks scalar tail, void tail, and no tail.
cat > "$tempdir/gnu_statement_expression_dataflow.i" <<'EOF'
static int state;
static int add_three(void) { state += 3; return state; }
static void add_five(void) { state += 5; }
int main(void) {
    int returned = ({ state += 2; add_three(); });
    (void)({ add_five(); });
    ({ state += 1; ; });
    return returned == 5 && state == 11 ? 42 : 1;
}
EOF
check_exit gnu_statement_expression_dataflow 42

# Preserve qualified scalar load/store, prefix/postfix value selection and
# pointer compound RMW while one Core owner constructs all memory effects.
cat > "$tempdir/core_scalar_memory_effects.i" <<'EOF'
static volatile int global = 19;
static int values[3] = { 8, 42, 9 };
int main(void) {
    int old = global++;
    int updated = ++global;
    int *cursor = &values[0];
    if (old != 19 || updated != 21 || global != 21) return 1;
    cursor += 1;
    if (*cursor != 42) return 2;
    cursor -= 1;
    if (*cursor != 8) return 3;
    global = 42;
    return global;
}
EOF
check_exit core_scalar_memory_effects 42

# Core's pointer compound owner must restore the pointer and prior value
# after a CFG-producing RHS. Aggregate return-object terminators are not yet
# supported by the x86 native backend (x86_emit_terminator rejects them), so
# keep record-call member acceptance in the capable cross-target oracles.
cat > "$tempdir/core_pointer_cfg_rmw.i" <<'EOF'
static int index_calls;
static int next_index(void) { index_calls++; return 1; }
int main(void) {
    int values[3] = { 8, 42, 9 };
    int *cursor = &values[0];
    cursor += index_calls == 0 ? next_index() : 2;
    if (*cursor != 42 || index_calls != 1) return 1;
    cursor -= index_calls == 1 ? next_index() : 2;
    if (*cursor != 8 || index_calls != 2) return 2;
    return 42;
}
EOF
check_exit core_pointer_cfg_rmw 42

# C function-scope labels and block-scoped GNU __label__ must keep the
# original forward reference, cleanup lifetime and goto binding semantics.
cat > "$tempdir/forward_goto_local_label.i" <<'EOF'
int main(void) {
    int value = 0;
    goto ordinary;
    value = 100;
ordinary:
    value = 40;
    {
        __label__ local;
        goto local;
        value = 100;
local:
        value += 2;
    }
    return value;
}
EOF
check_exit forward_goto_local_label 42

# Branch, loop and switch parsing must preserve AST block ownership,
# fallthrough, break/continue targets and for expression side-effect order.
cat > "$tempdir/statement_control_cfg_dataflow.i" <<'EOF'
int main(void) {
    int total = 0;
    int i = 0;
    for (; i < 4; ++i) {
        if (i == 1) continue;
        switch (i) {
            case 0: total += 2; break;
            case 2: total += 20; break;
            default: total += 4;
        }
    }
    i = 0;
    while (i < 2) { total += 3; ++i; }
    do { total += 7; } while (0);
    return total + 3;
}
EOF
check_exit statement_control_cfg_dataflow 42

# One action may be overwritten across a designated range; ownership must be
# last-wins for static and runtime arrays without quadratic element/action
# rescans. Runtime nonoverridden range expressions evaluate only once.
cat > "$tempdir/initializer_range_ownership.i" <<'EOF'
static int static_values[64] = {
    [0 ... 63] = 1,
    [9 ... 20] = 3,
    [17] = 42
};
static int calls;
static int value(void) { ++calls; return 7; }
int main(void) {
    int automatic[64] = {
        [0 ... 63] = 2,
        [9 ... 20] = value(),
        [17] = 42
    };
    if (static_values[0] != 1 || static_values[12] != 3 ||
        static_values[17] != 42 || static_values[22] != 1)
        return 1;
    if (automatic[0] != 2 || automatic[12] != 7 ||
        automatic[17] != 42 || automatic[22] != 2 || calls != 1)
        return 2;
    return static_values[17];
}
EOF
check_exit initializer_range_ownership 42

# The final-owner snapshot must also preserve static record-array ranges and
# skip fully overridden runtime expression side effects without reordering.
cat > "$tempdir/initializer_record_owner_index.i" <<'EOF'
struct Pair { int a; int b; };
static struct Pair values[4] = {
    [0 ... 3] = { 1, 2 }, [1 ... 2] = { 3, 4 }, [2] = { 5, 6 }
};
static int calls;
static int evaluate(int value) { ++calls; return value; }
int main(void) {
    int local[7] = {
        [0 ... 6] = evaluate(99), [0 ... 6] = evaluate(6),
        [1 ... 3] = evaluate(12), [4] = evaluate(3),
        [6] = evaluate(1)
    };
    if (values[0].a != 1 || values[0].b != 2 ||
        values[1].a != 3 || values[1].b != 4 ||
        values[2].a != 5 || values[2].b != 6 ||
        values[3].a != 1 || values[3].b != 2) return 1;
    if (local[0] != 6 || local[1] != 12 || local[2] != 12 ||
        local[3] != 12 || local[4] != 3 || local[5] != 6 ||
        local[6] != 1 || calls != 4) return 2;
    return 42;
}
EOF
check_exit initializer_record_owner_index 42

# Shared relocation replay: large sorted pointer tables avoid quadratic
# insertion scans while rebased object/function/record targets remain correct.
cat > "$tempdir/initializer_relocation_owners.i" <<'EOF'
static int values[4] = {5, 7, 9, 11};
static int *const slots[512] = {
    [0 ... 511] = &values[1], [2] = &values[3], [511] = &values[0]
};
static int answer(void) { return 42; }
static int (*call)(void) = answer;
struct Pair { int *target; int tag; };
static struct Pair pairs[3] = {
    [0 ... 2] = { &values[1], 3 }, [1] = { &values[2], 5 }
};
int main(void) {
    if (slots[0] != &values[1] || slots[2] != &values[3] ||
        slots[250] != &values[1] || slots[511] != &values[0]) return 1;
    if (pairs[0].target != &values[1] || pairs[1].target != &values[2] ||
        pairs[2].target != &values[1] || pairs[1].tag != 5) return 2;
    if (call() != 42) return 3;
    return 42;
}
EOF
check_exit initializer_relocation_owners 42

# Sparse static initializer materialization must not allocate an extra
# full-array pointer-action payload; unowned slots zero-fill and both object
# and function relocations remain associated with the selected owner.
cat > "$tempdir/initializer_sparse_stream.i" <<'EOF'
static int table[4] = {5, 7, 9, 11};
static int *const sparse[2048] = {
    [512] = &table[2],
    [1024 ... 1026] = &table[0],
    [1025] = &table[3],
    [2047] = &table[1]
};
static int integers[4096] = {
    [100 ... 300] = 4, [200 ... 250] = 7, [4095] = 42
};
int main(void) {
    if (sparse[0] != 0 || sparse[511] != 0 || sparse[512] != &table[2] ||
        sparse[513] != 0 || sparse[1024] != &table[0] ||
        sparse[1025] != &table[3] || sparse[1026] != &table[0] ||
        sparse[1027] != 0 || sparse[2047] != &table[1])
        return 1;
    if (integers[0] != 0 || integers[150] != 4 ||
        integers[230] != 7 || integers[300] != 4 ||
        integers[301] != 0)
        return 2;
    return integers[4095];
}
EOF
check_exit initializer_sparse_stream 42

# One DataLayout walk now owns the flat record payload for all backends,
# while the target retains its GNU scalar directives and reloc spelling.
cat > "$tempdir/initializer_shared_flat_record.i" <<'EOF'
static long target_words[4] = {5, 7, 11, 13};
struct Flat { int count; double ratio; long *address; float factor; };
static struct Flat flat = {17, 3.5, &target_words[2], 1.25f};
static long *const addresses[5] = {
    &target_words[1], 0, &target_words[3],
    &target_words[0], &target_words[2]
};
int main(void) {
    if (flat.count != 17 || flat.ratio != 3.5 ||
        flat.address != &target_words[2] || flat.factor != 1.25f)
        return 1;
    if (addresses[0] != &target_words[1] || addresses[1] != 0 ||
        addresses[2] != &target_words[3] ||
        addresses[3] != &target_words[0] ||
        addresses[4] != &target_words[2])
        return 2;
    return 42;
}
EOF
check_exit initializer_shared_flat_record 42

# Shared recursive GlobalObject data layout for nested records and pointers;
# the per-target GNU numeric/relocation formatting stays independent.
cat > "$tempdir/initializer_nested_shared_layout.i" <<'EOF'
static int elements[4] = {3, 5, 7, 11};
struct Inner { unsigned short count; int *pointer; };
struct Outer { struct Inner members[2]; long sum; };
static struct Outer shared = {
    {{17, &elements[1]}, {29, &elements[3]}}, 41
};
int main(void) {
    if (shared.members[0].count != 17 ||
        shared.members[0].pointer != &elements[1] ||
        shared.members[1].count != 29 ||
        shared.members[1].pointer != &elements[3] ||
        shared.sum != 41)
        return 1;
    return 42;
}
EOF
check_exit initializer_nested_shared_layout 42

# Confirm the single common bitfield packer works in the flat record-array
# entry point, which formerly had a second bespoke 64-bit shift algorithm.
cat > "$tempdir/initializer_flat_bitfield_array.i" <<'EOF'
static long nums[4] = {3, 5, 7, 11};
struct BitRecord {
    long *ref;
    unsigned int mode:6;
    unsigned int options:10;
    unsigned short end;
};
static struct BitRecord entries[2] = {
    {&nums[1], 17, 301, 9},
    {&nums[3], 11, 203, 13}
};
int main(void) {
    return entries[0].ref == &nums[1] &&
           entries[0].mode == 17 && entries[0].options == 301 &&
           entries[0].end == 9 && entries[1].ref == &nums[3] &&
           entries[1].mode == 11 && entries[1].options == 203 &&
           entries[1].end == 13 ? 42 : 1;
}
EOF
check_exit initializer_flat_bitfield_array 42

# Ordinary record and its initialized GNU flexible tail must share one
# storage-extent calculation rather than two serialized byte walks.
cat > "$tempdir/initializer_mixed_flexible_record.i" <<'EOF'
static int pointed[3] = {5, 7, 11};
struct Mix {
    unsigned int a:6;
    unsigned int b:10;
    int *address;
    unsigned long tail[];
};
static struct Mix mix = {
    .a = 17, .b = 301, .address = &pointed[1],
    .tail = { [0 ... 2] = 11UL }
};
int main(void) {
    return mix.a == 17 && mix.b == 301 &&
           mix.address == &pointed[1] &&
           mix.tail[0] == 11 && mix.tail[1] == 11 &&
           mix.tail[2] == 11 ? 42 : 1;
}
EOF
check_exit initializer_mixed_flexible_record 42
