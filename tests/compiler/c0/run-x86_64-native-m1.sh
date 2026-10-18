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

# Real Linux default PIE: dynamic libc function *and object* address
# materialization must resolve via GOT, not the non-PIE PC32 LEA form.
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
"$MINIC_CC" --target=x86_64-linux -S "$tempdir/pie_external_symbols.i" \
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

# The x86 runtime's one-word va_list proxy is deliberately not yet
# GCC's C array[1] type (sizeof=24). Reject size queries rather than
# silently miscompile them as sizeof(void*)=8, through typedefs and locals.
cat > "$tempdir/varargs_sizeof_type_unsupported.i" <<'EOF'
typedef __builtin_va_list va_list;
int main(void) { return (int)sizeof(va_list); }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/varargs_sizeof_type_unsupported.i" \
    -o "$tempdir/varargs_sizeof_type_unsupported.s" \
    >"$tempdir/va_size_type.out" 2>"$tempdir/va_size_type.err"; then
  echo "FAIL: x86 va_list sizeof(type) miscompiled as one pointer" >&2
  exit 1
fi
test ! -e "$tempdir/varargs_sizeof_type_unsupported.s"
grep -F 'sizeof requires a supported complete type' "$tempdir/va_size_type.err" >/dev/null

cat > "$tempdir/varargs_sizeof_local_unsupported.i" <<'EOF'
typedef __builtin_va_list va_list;
int main(void) { va_list list; return (int)sizeof(list); }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/varargs_sizeof_local_unsupported.i" \
    -o "$tempdir/varargs_sizeof_local_unsupported.s" \
    >"$tempdir/va_size_local.out" 2>"$tempdir/va_size_local.err"; then
  echo "FAIL: x86 va_list sizeof(local) miscompiled as one pointer" >&2
  exit 1
fi
test ! -e "$tempdir/varargs_sizeof_local_unsupported.s"
grep -F 'sizeof requires a supported complete type' "$tempdir/va_size_local.err" >/dev/null

cat > "$tempdir/varargs_type_identity.i" <<'EOF'
typedef __builtin_va_list va_list;
_Static_assert(!__builtin_types_compatible_p(va_list, void *),
               "x86 SysV va_list has distinct type identity");
int main(void) { return 42; }
EOF
check_exit varargs_type_identity 42
"$HOST_CC" -x c -std=gnu11 -c -o "$tempdir/gcc-va-type.o" \
    "$tempdir/varargs_type_identity.i"
echo "PASS: x86 SysV va_list opaque cursor identity and fail-closed sizeof"

# Until true destination-owned va_copy tagging is designed, do not silently
# turn the SysV cursor copy into a pointer alias (RV64 semantics are unchanged).
cat > "$tempdir/varargs_copy_unsupported.i" <<'EOF'
typedef __builtin_va_list va_list;
int bad_copy(int fixed, ...) {
    va_list first, second;
    __builtin_va_start(first, fixed);
    __builtin_va_copy(second, first);
    __builtin_va_end(first);
    __builtin_va_end(second);
    return 0;
}
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/varargs_copy_unsupported.i" \
    -o "$tempdir/varargs_copy_unsupported.s" >"$tempdir/va_copy.out" 2>"$tempdir/va_copy.err"; then
    echo "FAIL: unsafe SysV x86 va_copy pointer alias was accepted" >&2
    exit 1
fi
test ! -e "$tempdir/varargs_copy_unsupported.s"
echo "PASS: SysV va_copy without independent cursor intentionally fails closed"




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
