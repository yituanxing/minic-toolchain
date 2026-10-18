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








# Nested aggregate flattening still needs an explicit implementation.
cat > "$tempdir/unsupported.i" <<'EOF'
struct Pair { int a; int b; };
struct Nested { struct Pair pair; int c; };
static struct Nested value = {{20, 22}, 1};
int main(void) { return value.pair.a; }
EOF
if "$MINIC_CC" --target=x86_64-linux -S "$tempdir/unsupported.i" \
    -o "$tempdir/unsupported.s" >"$tempdir/unsupported.stdout" 2>"$tempdir/unsupported.stderr"; then
  echo "FAIL: unsupported x86 nested aggregate initializer incorrectly accepted" >&2
  exit 1
fi
test ! -e "$tempdir/unsupported.s"
grep -F 'x86-64 global initializer/relocation unsupported:' "$tempdir/unsupported.stderr" >/dev/null
echo "PASS: x86-64 unsupported nested aggregate global fails closed"
