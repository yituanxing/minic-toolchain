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
if [ "${FOUR_BACKEND_M2:-0}" = "1" ] || [ "${FOUR_BACKEND_M2_STACK:-0}" = "1" ] || [ "${FOUR_BACKEND_M3_CALL:-0}" = "1" ] || [ "${FOUR_BACKEND_M4_GLOBAL:-0}" = "1" ] || [ "${FOUR_BACKEND_M5_POINTER:-0}" = "1" ] || [ "${FOUR_BACKEND_M6_ADDRESS:-0}" = "1" ] || [ "${FOUR_BACKEND_M7_FRAME:-0}" = "1" ] || [ "${FOUR_BACKEND_M8_NARROW:-0}" = "1" ] || [ "${FOUR_BACKEND_M9_UNSIGNED:-0}" = "1" ] || [ "${FOUR_BACKEND_M10_MOD:-0}" = "1" ] || [ "${FOUR_BACKEND_M11_INDEX:-0}" = "1" ] || [ "${FOUR_BACKEND_M12_BITWISE:-0}" = "1" ] || [ "${FOUR_BACKEND_M13_NARROW_ARITH:-0}" = "1" ] || [ "${FOUR_BACKEND_M14_SHIFT:-0}" = "1" ] || [ "${FOUR_BACKEND_M15_SCALAR_RETURN:-0}" = "1" ] || [ "${FOUR_BACKEND_M17_RECORD:-0}" = "1" ] || [ "${FOUR_BACKEND_M18_VARARGS:-0}" = "1" ] || [ "${FOUR_BACKEND_M19_PTR_COMPARE:-0}" = "1" ] || [ "${FOUR_BACKEND_M20_LP64_ABI:-0}" = "1" ] || [ "${FOUR_BACKEND_M21_LP64_ADD:-0}" = "1" ] || [ "${FOUR_BACKEND_M22_LP64_CONV:-0}" = "1" ] || [ "${FOUR_BACKEND_M23_LP64_COMPARE:-0}" = "1" ] || [ "${FOUR_BACKEND_M24_INDIRECT_CALL:-0}" = "1" ]; then
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
# Distinct original-source global arrays must not silently alias through
# a target GOT relocation. Early AArch64 emitted the *correct* byte
# initializers for vector_key but read the neighboring AES sbox instead.
cat > "$w/global_array.i" <<'EOF'
const unsigned char mini_table[4] = { 7, 8, 9, 10 };
const unsigned char mini_second_table[4] = { 99, 35, 254, 118 };
int mini_global_array(void) {
    return (int)mini_table[0] + (int)mini_second_table[1];
}
EOF
cat > "$w/driver_global_array.c" <<'EOF'
extern const unsigned char mini_table[4];
extern const unsigned char mini_second_table[4];
extern int mini_global_array(void);
int main(void) {
    if (mini_table[0] != 7 || mini_table[1] != 8 ||
        mini_table[2] != 9 || mini_table[3] != 10 ||
        mini_second_table[0] != 99 || mini_second_table[1] != 35 ||
        mini_second_table[2] != 254 || mini_second_table[3] != 118)
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
# M7: enforce real > 2KiB Core frames, rather than merely lifting
# the old 240-value guard. The same C inputs are GNU-built as oracles.
# Keep this within the deliberately bounded 8KiB frame for now.
{
    printf 'int mini_large_frame(int x) {\n  int v0 = x;\n'
    j=1
    while [ "$j" -le 72 ]; do
        before=$((j - 1))
        printf '  int v%s = v%s + 1;\n' "$j" "$before"
        j=$((j + 1))
    done
    printf '  return v72 - 30;\n}\n'
} > "$w/large_frame.i"
cat > "$w/driver_large_frame.c" <<'EOF'
extern int mini_large_frame(int);
int main(void) { return mini_large_frame(0); }
EOF
cat > "$w/local_array.i" <<'EOF'
int mini_local_array(void) {
    int slots[3];
    slots[0] = 7;
    slots[1] = 42;
    slots[2] = 11;
    return slots[1];
}
EOF
cat > "$w/driver_local_array.c" <<'EOF'
extern int mini_local_array(void);
int main(void) { return mini_local_array(); }
EOF
# M8: the *original* KeyExpansion uses both narrow byte loads and
# Core convert.int. Preserve signedness across two target ABIs.
cat > "$w/ubyte_read.i" <<'EOF'
int mini_ubyte_read(unsigned char *p) { return *p; }
EOF
cat > "$w/driver_ubyte_read.c" <<'EOF'
extern int mini_ubyte_read(unsigned char *);
int main(void) { unsigned char x = 42; return mini_ubyte_read(&x); }
EOF
cat > "$w/sbyte_read.i" <<'EOF'
int mini_sbyte_read(signed char *p) { return *p + 43; }
EOF
cat > "$w/driver_sbyte_read.c" <<'EOF'
extern int mini_sbyte_read(signed char *);
int main(void) { signed char x = -1; return mini_sbyte_read(&x); }
EOF
cat > "$w/byte_write.i" <<'EOF'
void mini_byte_write(unsigned char *p) { *p = 42; }
EOF
cat > "$w/driver_byte_write.c" <<'EOF'
extern void mini_byte_write(unsigned char *);
int main(void) { unsigned char x = 0; mini_byte_write(&x); return x; }
EOF
cat > "$w/ushort_read.i" <<'EOF'
int mini_ushort_read(unsigned short *p) { return *p; }
EOF
cat > "$w/driver_ushort_read.c" <<'EOF'
extern int mini_ushort_read(unsigned short *);
int main(void) { unsigned short x = 42; return mini_ushort_read(&x); }
EOF
cat > "$w/short_read.i" <<'EOF'
int mini_short_read(short *p) { return *p + 45; }
EOF
cat > "$w/driver_short_read.c" <<'EOF'
extern int mini_short_read(short *);
int main(void) { short x = -3; return mini_short_read(&x); }
EOF
cat > "$w/uchar_conv.i" <<'EOF'
int mini_uchar_conv(int value) { return (unsigned char)value; }
EOF
cat > "$w/driver_uchar_conv.c" <<'EOF'
extern int mini_uchar_conv(int);
int main(void) { return mini_uchar_conv(298); }
EOF
cat > "$w/ushort_conv.i" <<'EOF'
int mini_ushort_conv(int value) { return (unsigned short)value; }
EOF
cat > "$w/driver_ushort_conv.c" <<'EOF'
extern int mini_ushort_conv(int);
int main(void) { return mini_ushort_conv(65578); }
EOF
# M9: original KeyExpansion loop uses uint32_t counters and unsigned
# comparisons; x86 semantics must be kept, target instruction differs.
cat > "$w/unsigned_less.i" <<'EOF'
int mini_unsigned_less(unsigned int x) { return x < 3u ? 1 : 42; }
EOF
cat > "$w/driver_unsigned_less.c" <<'EOF'
extern int mini_unsigned_less(unsigned int);
int main(void) { return mini_unsigned_less(0xffffffffu); }
EOF
cat > "$w/unsigned_add.i" <<'EOF'
int mini_unsigned_add(unsigned int x) { return (int)(x + 43u); }
EOF
cat > "$w/driver_unsigned_add.c" <<'EOF'
extern int mini_unsigned_add(unsigned int);
int main(void) { return mini_unsigned_add(0xffffffffu); }
EOF
cat > "$w/unsigned_index.i" <<'EOF'
int mini_unsigned_index(int *p, unsigned int index) {
    return p[index];
}
EOF
cat > "$w/driver_unsigned_index.c" <<'EOF'
extern int mini_unsigned_index(int *, unsigned int);
int main(void) {
    int a[3] = { 10, 25, 42 };
    return mini_unsigned_index(a, 2u);
}
EOF
# M10: original AES KeyExpansion unsigned i %% Nk is a real
# signedness-sensitive remainder, not a signed div shortcut.
cat > "$w/umod.i" <<'EOF'
int mini_umod(unsigned int x) { return 40 + (int)(x % 3u); }
EOF
cat > "$w/driver_umod.c" <<'EOF'
extern int mini_umod(unsigned int);
int main(void) { return mini_umod(0x80000000u); }
EOF
cat > "$w/smod.i" <<'EOF'
int mini_smod(int x) { return 43 + x % 3; }
EOF
cat > "$w/driver_smod.c" <<'EOF'
extern int mini_smod(int);
int main(void) { return mini_smod(-7); }
EOF
cat > "$w/udiv.i" <<'EOF'
int mini_udiv(unsigned int x) { return 39 + (int)(x / 0x55555555u); }
EOF
cat > "$w/driver_udiv.c" <<'EOF'
extern int mini_udiv(unsigned int);
int main(void) { return mini_udiv(0xffffffffu); }
EOF
cat > "$w/sdiv.i" <<'EOF'
int mini_sdiv(int x) { return 84 + x / 3; }
EOF
cat > "$w/driver_sdiv.c" <<'EOF'
extern int mini_sdiv(int);
int main(void) { return mini_sdiv(-126); }
EOF
# M11: real original AES KeyExpansion indexes a byte table using an
# 8-bit integer. Compare signed/unsigned pointer-offset semantics to GNU,
# including sign extension across the LP64 address arithmetic boundary.
cat > "$w/u8_index.i" <<'EOF'
int mini_u8_index(const int *p, unsigned char index) { return p[index]; }
EOF
cat > "$w/driver_u8_index.c" <<'EOF'
extern int mini_u8_index(const int *, unsigned char);
int main(void) { int slots[220] = { 0 }; slots[200] = 42; return mini_u8_index(slots, 200); }
EOF
cat > "$w/s8_index.i" <<'EOF'
int mini_s8_index(const int *p, signed char index) { return p[index]; }
EOF
cat > "$w/driver_s8_index.c" <<'EOF'
extern int mini_s8_index(const int *, signed char);
int main(void) { int slots[3] = { 9, 42, 7 }; return mini_s8_index(&slots[2], -1); }
EOF
cat > "$w/u16_index.i" <<'EOF'
int mini_u16_index(const int *p, unsigned short index) { return p[index]; }
EOF
cat > "$w/driver_u16_index.c" <<'EOF'
extern int mini_u16_index(const int *, unsigned short);
int main(void) { int slots[220] = { 0 }; slots[210] = 42; return mini_u16_index(slots, 210); }
EOF
cat > "$w/s16_index.i" <<'EOF'
int mini_s16_index(const int *p, short index) { return p[index]; }
EOF
cat > "$w/driver_s16_index.c" <<'EOF'
extern int mini_s16_index(const int *, short);
int main(void) { int slots[3] = { 10, 42, 15 }; return mini_s16_index(&slots[2], -1); }
EOF
# M12: actual AES KeyExpansion now reaches INTEGER_BITWISE_XOR.
# Cross-GNU oracles cover all three binary bitwise operators and a
# high-bit unsigned case, executing the MiniC object under each QEMU.
cat > "$w/bit_xor.i" <<'EOF'
int mini_bit_xor(int a, int b) { return a ^ b; }
EOF
cat > "$w/driver_bit_xor.c" <<'EOF'
extern int mini_bit_xor(int,int);
int main(void) { return mini_bit_xor(21, 63); }
EOF
cat > "$w/bit_and.i" <<'EOF'
int mini_bit_and(int a, int b) { return a & b; }
EOF
cat > "$w/driver_bit_and.c" <<'EOF'
extern int mini_bit_and(int,int);
int main(void) { return mini_bit_and(127, 42); }
EOF
cat > "$w/bit_or.i" <<'EOF'
int mini_bit_or(int a, int b) { return a | b; }
EOF
cat > "$w/driver_bit_or.c" <<'EOF'
extern int mini_bit_or(int,int);
int main(void) { return mini_bit_or(40, 2); }
EOF
cat > "$w/bit_unsigned.i" <<'EOF'
int mini_bit_unsigned(unsigned int n) { return (int)((n ^ 255u) & 255u); }
EOF
cat > "$w/driver_bit_unsigned.c" <<'EOF'
extern int mini_bit_unsigned(unsigned int);
int main(void) { return mini_bit_unsigned(0xffffffd5u); }
EOF
# M13: the first remaining real AddRoundKey Core ADD uses uint8_t.
# Validate all 8/16-bit arithmetic wrap/extension paths through actual
# GNU→MiniC executable cross-TU calls, not just assembly inspection.
cat > "$w/u8_wrap.i" <<'EOF'
int mini_u8_wrap(void) { unsigned char i = 255; ++i; return (int)i + 42; }
EOF
cat > "$w/driver_u8_wrap.c" <<'EOF'
extern int mini_u8_wrap(void);
int main(void) { return mini_u8_wrap(); }
EOF
cat > "$w/u8_sub.i" <<'EOF'
int mini_u8_sub(void) { unsigned char i = 0; --i; return (int)i - 213; }
EOF
cat > "$w/driver_u8_sub.c" <<'EOF'
extern int mini_u8_sub(void);
int main(void) { return mini_u8_sub(); }
EOF
cat > "$w/u8_mul.i" <<'EOF'
int mini_u8_mul(void) { unsigned char i = 100; i *= 3; return (int)i - 2; }
EOF
cat > "$w/driver_u8_mul.c" <<'EOF'
extern int mini_u8_mul(void);
int main(void) { return mini_u8_mul(); }
EOF
cat > "$w/u16_wrap.i" <<'EOF'
int mini_u16_wrap(void) { unsigned short i = 65535; ++i; return (int)i + 42; }
EOF
cat > "$w/driver_u16_wrap.c" <<'EOF'
extern int mini_u16_wrap(void);
int main(void) { return mini_u16_wrap(); }
EOF
cat > "$w/s8_add.i" <<'EOF'
int mini_s8_add(void) { signed char i = -5; i += 2; return (int)i + 45; }
EOF
cat > "$w/driver_s8_add.c" <<'EOF'
extern int mini_s8_add(void);
int main(void) { return mini_s8_add(); }
EOF
# M14: real xtime uses x<<1 and x>>7 with C integer promotions.
# Signed right-shift and narrow results each need independent machine
# instructions and effective-width normalization.
cat > "$w/shift_left.i" <<'EOF'
int mini_shift_left(int x,int bits) { return x << bits; }
EOF
cat > "$w/driver_shift_left.c" <<'EOF'
extern int mini_shift_left(int,int);
int main(void) { return mini_shift_left(21,1); }
EOF
cat > "$w/shift_unsigned.i" <<'EOF'
int mini_shift_unsigned(unsigned int x) { return (int)(x >> 1) - 1; }
EOF
cat > "$w/driver_shift_unsigned.c" <<'EOF'
extern int mini_shift_unsigned(unsigned int);
int main(void) { return mini_shift_unsigned(86u); }
EOF
cat > "$w/shift_signed.i" <<'EOF'
int mini_shift_signed(int x) { return (x >> 2) + 45; }
EOF
cat > "$w/driver_shift_signed.c" <<'EOF'
extern int mini_shift_signed(int);
int main(void) { return mini_shift_signed(-12); }
EOF
cat > "$w/shift_byte.i" <<'EOF'
int mini_shift_byte(unsigned char x) { unsigned char r = (unsigned char)(x << 1); return (int)r; }
EOF
cat > "$w/driver_shift_byte.c" <<'EOF'
extern int mini_shift_byte(unsigned char);
int main(void) { return mini_shift_byte(149u); }
EOF
cat > "$w/shift_count.i" <<'EOF'
int mini_shift_count(unsigned short x, unsigned char count) { return (int)(x << count) - 42; }
EOF
cat > "$w/driver_shift_count.c" <<'EOF'
extern int mini_shift_count(unsigned short,unsigned char);
int main(void) { return mini_shift_count(21u,2u); }
EOF
# M15: authentic xtime returns uint8_t and GNU callers observe the
# low-byte ABI; test both external and internally nested direct calls.
cat > "$w/u8_return.i" <<'EOF'
unsigned char mini_u8_return(unsigned char x) { return (unsigned char)(x + 43); }
EOF
cat > "$w/driver_u8_return.c" <<'EOF'
extern unsigned char mini_u8_return(unsigned char);
int main(void) { return (int)mini_u8_return(255u); }
EOF
cat > "$w/s8_return.i" <<'EOF'
signed char mini_s8_return(signed char x) { return x; }
EOF
cat > "$w/driver_s8_return.c" <<'EOF'
extern signed char mini_s8_return(signed char);
int main(void) { return (int)mini_s8_return(-3) + 45; }
EOF
cat > "$w/u8_external.i" <<'EOF'
extern unsigned char gnu_u8(unsigned char);
int mini_u8_external(unsigned char x) { return (int)gnu_u8(x); }
EOF
cat > "$w/driver_u8_external.c" <<'EOF'
extern int mini_u8_external(unsigned char);
unsigned char gnu_u8(unsigned char x) { return (unsigned char)(x + 1); }
int main(void) { return mini_u8_external(41u); }
EOF
cat > "$w/u8_internal.i" <<'EOF'
static unsigned char mini_u8_helper(unsigned char x) { return (unsigned char)(x + 1); }
int mini_u8_internal(unsigned char x) { return (int)mini_u8_helper(x); }
EOF
cat > "$w/driver_u8_internal.c" <<'EOF'
extern int mini_u8_internal(unsigned char);
int main(void) { return mini_u8_internal(41u); }
EOF
# M17: the untouched original cJSON uses error {NULL,0} and
# hooks {malloc,free,realloc}; verify target DataLayout field padding
# and real ELF object/function relocation behavior via GNU consumers.
cat > "$w/record_zero.i" <<'EOF'
struct Err { void *json; unsigned long position; };
struct Err mini_error = { (void *)0, 0 };
int mini_record_zero(void) { return 42; }
EOF
cat > "$w/driver_record_zero.c" <<'EOF'
struct Err { void *json; unsigned long position; };
extern struct Err mini_error;
extern int mini_record_zero(void);
int main(void) {
    return mini_error.json == 0 && mini_error.position == 0
           ? mini_record_zero() : 1;
}
EOF
cat > "$w/record_hooks.i" <<'EOF'
extern int gnu_helper(int);
int mini_helper(int x) { return x; }
struct Hooks { int (*one)(int); int (*two)(int); void *context; };
struct Hooks mini_hooks = {mini_helper, gnu_helper, (void *)0};
int mini_record_hooks(void) { return 42; }
EOF
cat > "$w/driver_record_hooks.c" <<'EOF'
int gnu_helper(int x) { return x + 1; }
struct Hooks { int (*one)(int); int (*two)(int); void *context; };
extern struct Hooks mini_hooks;
extern int mini_record_hooks(void);
int main(void) {
    if (mini_hooks.one == 0 || mini_hooks.two == 0 ||
        mini_hooks.context != 0 ||
        mini_hooks.one(40) != 40 || mini_hooks.two(40) != 41)
        return 1;
    return mini_record_hooks();
}
EOF
cat > "$w/record_padding.i" <<'EOF'
struct Pad { unsigned char lead; int n; unsigned char tail; };
struct Pad mini_pad = { 3, 42, 6 };
int mini_record_padding(void) { return 42; }
EOF
cat > "$w/driver_record_padding.c" <<'EOF'
struct Pad { unsigned char lead; int n; unsigned char tail; };
extern struct Pad mini_pad;
extern int mini_record_padding(void);
int main(void) {
    if (mini_pad.lead != 3 || mini_pad.n != 42 ||
        mini_pad.tail != 6 || sizeof(mini_pad) != 12)
        return 1;
    return mini_record_padding();
}
EOF
# M18: the original cJSON_Version uses sprintf(version, "%i.%i.%i",
# 1,7,19). Only promoted 32-bit integer and pointer varargs in the first
# eight argument registers are admitted; verify actual target C ABI.
cat > "$w/vararg_int.i" <<'EOF'
extern int sprintf(char *, const char *, ...);
int mini_vararg_int(char *buffer) {
    return sprintf(buffer, "%i.%i.%i", 1, 7, 19);
}
EOF
cat > "$w/driver_vararg_int.c" <<'EOF'
#include <string.h>
extern int mini_vararg_int(char *);
int main(void) {
    char b[32] = {0};
    return mini_vararg_int(b) == 6 &&
           strcmp(b, "1.7.19") == 0 ? 42 : 1;
}
EOF
cat > "$w/vararg_pointer.i" <<'EOF'
extern int sprintf(char *, const char *, ...);
int mini_vararg_pointer(char *buffer, const char *text) {
    return sprintf(buffer, "%s:%d", text, 7);
}
EOF
cat > "$w/driver_vararg_pointer.c" <<'EOF'
#include <string.h>
extern int mini_vararg_pointer(char *, const char *);
int main(void) {
    char b[32] = {0};
    return mini_vararg_pointer(b, "abc") == 5 &&
           strcmp(b, "abc:7") == 0 ? 42 : 1;
}
EOF
# M19: cJSON_InitHooks compares hook function pointers and tests
# hooks == NULL. GNU cross-built callers must observe full LP64 pointer
# equality and code addresses (not truncated w-reg comparisons).
cat > "$w/pointer_null.i" <<'EOF'
int mini_pointer_null(const void *p) { return p == 0 ? 42 : 1; }
EOF
cat > "$w/driver_pointer_null.c" <<'EOF'
extern int mini_pointer_null(const void *);
int main(void) { return mini_pointer_null((void *)0); }
EOF
cat > "$w/pointer_equality.i" <<'EOF'
int mini_pointer_equality(int *a, int *b) { return a == b ? 42 : 1; }
EOF
cat > "$w/driver_pointer_equality.c" <<'EOF'
extern int mini_pointer_equality(int *, int *);
int main(void) {
    int x = 19;
    return mini_pointer_equality(&x, &x);
}
EOF
cat > "$w/function_address.i" <<'EOF'
int mini_callback_fn(int x) { return x + 1; }
int mini_function_address(int (*fn)(int)) {
    if (fn == mini_callback_fn) return mini_callback_fn(41);
    return 1;
}
EOF
cat > "$w/driver_function_address.c" <<'EOF'
extern int mini_callback_fn(int);
extern int mini_function_address(int (*)(int));
int main(void) { return mini_function_address(mini_callback_fn); }
EOF
# M20: original cJSON_ParseWithLength has a size_t (LP64 unsigned long)
# argument. Prove no high-32-bit truncation in GNU->MiniC parameters,
# MiniC->GNU calls, return registers, and actual scalar pointer memory.
cat > "$w/size64_abi.i" <<'EOF'
extern unsigned long gnu_size_identity(unsigned long);
unsigned long mini_size_identity(unsigned long n) {
    unsigned long local = n;
    return gnu_size_identity(local);
}
void mini_size_store(unsigned long *out, unsigned long n) { *out = n; }
unsigned long mini_size_load(const unsigned long *in) { return *in; }
unsigned long mini_size_literal(void) { return 0x1234567887654321UL; }
EOF
cat > "$w/driver_size64_abi.c" <<'EOF'
extern unsigned long mini_size_identity(unsigned long);
extern void mini_size_store(unsigned long *, unsigned long);
extern unsigned long mini_size_load(const unsigned long *);
extern unsigned long mini_size_literal(void);
unsigned long gnu_size_identity(unsigned long n) { return n; }
int main(void) {
    unsigned long n = 0x1234567887654321UL, v = 0;
    if (mini_size_identity(n) != n) return 1;
    mini_size_store(&v, n);
    if (mini_size_load(&v) != n) return 2;
    if (mini_size_literal() != n) return 3;
    return 42;
}
EOF
# M21: source-derived cJSON size_t arithmetic. Verify 64-bit carry,
# borrow and signed 64-bit addition against independently GNU built C.
cat > "$w/size64_add.i" <<'EOF'
unsigned long mini_size_add(unsigned long a, unsigned long b) { return a + b; }
unsigned long mini_size_sub(unsigned long a, unsigned long b) { return a - b; }
long mini_signed_add(long a, long b) { return a + b; }
EOF
cat > "$w/driver_size64_add.c" <<'EOF'
extern unsigned long mini_size_add(unsigned long, unsigned long);
extern unsigned long mini_size_sub(unsigned long, unsigned long);
extern long mini_signed_add(long, long);
int main(void) {
    unsigned long a = 0x1234567887654321UL;
    unsigned long b = 0x10000001UL;
    if (mini_size_add(a,b) != a+b) return 1;
    if (mini_size_sub(a,b) != a-b) return 2;
    if (mini_size_sub(0,1) != ~0UL) return 3;
    if (mini_signed_add(-0x123456789L,17) != -0x123456789L+17) return 4;
    return 42;
}
EOF
# M22: cJSON's local 0 -> size_t conversion, plus signed/unsigned
# extensions and 64 -> narrow truncation: exact GNU LP64 semantics.
cat > "$w/size64_conv.i" <<'EOF'
unsigned long mini_from_int(int n) { return (unsigned long)n; }
unsigned long mini_from_uint(unsigned int n) { return (unsigned long)n; }
unsigned long mini_from_short(unsigned short n) { return (unsigned long)n; }
unsigned int mini_truncate_ulong(unsigned long n) { return (unsigned int)n; }
unsigned char mini_truncate_byte(unsigned long n) { return (unsigned char)n; }
unsigned long mini_long_zero(void) { return (unsigned long)0; }
EOF
cat > "$w/driver_size64_conv.c" <<'EOF'
extern unsigned long mini_from_int(int);
extern unsigned long mini_from_uint(unsigned int);
extern unsigned long mini_from_short(unsigned short);
extern unsigned int mini_truncate_ulong(unsigned long);
extern unsigned char mini_truncate_byte(unsigned long);
extern unsigned long mini_long_zero(void);
int main(void) {
    if (mini_from_int(-1) != ~0UL) return 1;
    if (mini_from_uint(0x80000001u) != 0x80000001UL) return 2;
    if (mini_from_short(0xabcd) != 0xabcdUL) return 3;
    if (mini_truncate_ulong(0x1234567887654321UL) != 0x87654321u) return 4;
    if (mini_truncate_byte(0x12345678876543a1UL) != 0xa1) return 5;
    if (mini_long_zero() != 0UL) return 6;
    return 42;
}
EOF
# M23: source-derived cJSON buffer_length comparison. Check the full
# 64-bit operands, distinct upper halves, and signed vs unsigned ordering.
cat > "$w/size64_compare.i" <<'EOF'
int mini_size_equal(unsigned long a, unsigned long b) { return a == b; }
int mini_size_less(unsigned long a, unsigned long b) { return a < b; }
int mini_signed_less(long a, long b) { return a < b; }
int mini_size_zero(unsigned long n) { return !n; }
int mini_size_truth(unsigned long n) { return n ? 42 : 1; }
EOF
cat > "$w/driver_size64_compare.c" <<'EOF'
extern int mini_size_equal(unsigned long, unsigned long);
extern int mini_size_less(unsigned long, unsigned long);
extern int mini_signed_less(long, long);
extern int mini_size_zero(unsigned long);
extern int mini_size_truth(unsigned long);
int main(void) {
    const unsigned long a = 0x1234567887654321UL;
    const unsigned long b = 0x9abcdef087654321UL;
    if (!mini_size_equal(a, a) || mini_size_equal(a, b)) return 1;
    if (!mini_size_less(a, b) || mini_size_less(b, a)) return 2;
    if (!mini_size_less(0x7fffffffffffffffUL, 0x8000000000000000UL)) return 3;
    if (!mini_signed_less(-0x123456789L, 17L)) return 4;
    if (mini_signed_less(17L, -0x123456789L)) return 5;
    if (!mini_size_zero(0) || mini_size_zero(0x100000000UL)) return 6;
    if (mini_size_truth(0x100000000UL) != 42 ||
        mini_size_truth(0) != 1) return 7;
    return 42;
}
EOF
# M24: cJSON_PrintBuffered uses indirect hooks.allocate and deallocate.
# Independent GNU caller/callees check the actual machine-call ABI, full
# LP64 argument, pointer return, and void/effect-only callback.
cat > "$w/indirect_hooks.i" <<'EOF'
struct MiniHooks {
    void *(*allocate)(unsigned long);
    void (*deallocate)(void *);
};
extern struct MiniHooks gnu_hooks;
int mini_indirect_hooks(unsigned long n) {
    void *p = gnu_hooks.allocate(n);
    if (p == (void *)0) return 1;
    gnu_hooks.deallocate(p);
    return 42;
}
unsigned long mini_indirect_size(unsigned long (*callback)(unsigned long),
                                 unsigned long n) {
    return callback(n);
}
void mini_indirect_void(void (*callback)(int), int n) { callback(n); }
EOF
cat > "$w/driver_indirect_hooks.c" <<'EOF'
struct MiniHooks {
    void *(*allocate)(unsigned long);
    void (*deallocate)(void *);
};
extern int mini_indirect_hooks(unsigned long);
extern unsigned long mini_indirect_size(unsigned long (*)(unsigned long),
                                        unsigned long);
extern void mini_indirect_void(void (*)(int), int);
static unsigned char storage[4];
static int released;
static int void_value;
static void *gnu_allocate(unsigned long n) {
    return n == 0x1234567887654321UL ? storage : 0;
}
static void gnu_deallocate(void *p) { if (p == storage) released = 1; }
static unsigned long gnu_identity(unsigned long n) { return n; }
static void gnu_void(int n) { void_value = n; }
struct MiniHooks gnu_hooks = {gnu_allocate, gnu_deallocate};
int main(void) {
    if (mini_indirect_hooks(0x1234567887654321UL) != 42 || !released)
        return 1;
    if (mini_indirect_size(gnu_identity, 0x1234567887654321UL) !=
        0x1234567887654321UL)
        return 2;
    mini_indirect_void(gnu_void, 29);
    return void_value == 29 ? 42 : 3;
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
    if [ "${FOUR_BACKEND_M7_FRAME:-0}" = "1" ]; then
        cases="$cases large_frame local_array"
    fi
    if [ "${FOUR_BACKEND_M8_NARROW:-0}" = "1" ]; then
        cases="$cases ubyte_read sbyte_read byte_write ushort_read short_read uchar_conv ushort_conv"
    fi
    if [ "${FOUR_BACKEND_M9_UNSIGNED:-0}" = "1" ]; then
        cases="$cases unsigned_less unsigned_add unsigned_index"
    fi
    if [ "${FOUR_BACKEND_M10_MOD:-0}" = "1" ]; then
        cases="$cases umod smod udiv sdiv"
    fi
    if [ "${FOUR_BACKEND_M11_INDEX:-0}" = "1" ]; then
        cases="$cases u8_index s8_index u16_index s16_index"
    fi
    if [ "${FOUR_BACKEND_M12_BITWISE:-0}" = "1" ]; then
        cases="$cases bit_xor bit_and bit_or bit_unsigned"
    fi
    if [ "${FOUR_BACKEND_M13_NARROW_ARITH:-0}" = "1" ]; then
        cases="$cases u8_wrap u8_sub u8_mul u16_wrap s8_add"
    fi
    if [ "${FOUR_BACKEND_M14_SHIFT:-0}" = "1" ]; then
        cases="$cases shift_left shift_unsigned shift_signed shift_byte shift_count"
    fi
    if [ "${FOUR_BACKEND_M15_SCALAR_RETURN:-0}" = "1" ]; then
        cases="$cases u8_return s8_return u8_external u8_internal"
    fi
    if [ "${FOUR_BACKEND_M17_RECORD:-0}" = "1" ]; then
        cases="$cases record_zero record_hooks record_padding"
    fi
    if [ "${FOUR_BACKEND_M18_VARARGS:-0}" = "1" ]; then
        cases="$cases vararg_int vararg_pointer"
    fi
    if [ "${FOUR_BACKEND_M19_PTR_COMPARE:-0}" = "1" ]; then
        cases="$cases pointer_null pointer_equality function_address"
    fi
    if [ "${FOUR_BACKEND_M20_LP64_ABI:-0}" = "1" ]; then
        cases="$cases size64_abi"
    fi
    if [ "${FOUR_BACKEND_M21_LP64_ADD:-0}" = "1" ]; then
        cases="$cases size64_add"
    fi
    if [ "${FOUR_BACKEND_M22_LP64_CONV:-0}" = "1" ]; then
        cases="$cases size64_conv"
    fi
    if [ "${FOUR_BACKEND_M23_LP64_COMPARE:-0}" = "1" ]; then
        cases="$cases size64_compare"
    fi
    if [ "${FOUR_BACKEND_M24_INDIRECT_CALL:-0}" = "1" ]; then
        cases="$cases indirect_hooks"
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
        if [ "${FOUR_BACKEND_M7_FRAME:-0}" = "1" ]; then
            oracle_cases="$oracle_cases large_frame local_array"
        fi
        if [ "${FOUR_BACKEND_M8_NARROW:-0}" = "1" ]; then
            oracle_cases="$oracle_cases ubyte_read sbyte_read byte_write ushort_read short_read uchar_conv ushort_conv"
        fi
        if [ "${FOUR_BACKEND_M9_UNSIGNED:-0}" = "1" ]; then
            oracle_cases="$oracle_cases unsigned_less unsigned_add unsigned_index"
        fi
        if [ "${FOUR_BACKEND_M10_MOD:-0}" = "1" ]; then
            oracle_cases="$oracle_cases umod smod udiv sdiv"
        fi
        if [ "${FOUR_BACKEND_M11_INDEX:-0}" = "1" ]; then
            oracle_cases="$oracle_cases u8_index s8_index u16_index s16_index"
        fi
        if [ "${FOUR_BACKEND_M12_BITWISE:-0}" = "1" ]; then
            oracle_cases="$oracle_cases bit_xor bit_and bit_or bit_unsigned"
        fi
        if [ "${FOUR_BACKEND_M13_NARROW_ARITH:-0}" = "1" ]; then
            oracle_cases="$oracle_cases u8_wrap u8_sub u8_mul u16_wrap s8_add"
        fi
        if [ "${FOUR_BACKEND_M14_SHIFT:-0}" = "1" ]; then
            oracle_cases="$oracle_cases shift_left shift_unsigned shift_signed shift_byte shift_count"
        fi
        if [ "${FOUR_BACKEND_M15_SCALAR_RETURN:-0}" = "1" ]; then
            oracle_cases="$oracle_cases u8_return s8_return u8_external u8_internal"
        fi
        if [ "${FOUR_BACKEND_M17_RECORD:-0}" = "1" ]; then
            oracle_cases="$oracle_cases record_zero record_hooks record_padding"
        fi
        if [ "${FOUR_BACKEND_M18_VARARGS:-0}" = "1" ]; then
            oracle_cases="$oracle_cases vararg_int vararg_pointer"
        fi
        if [ "${FOUR_BACKEND_M19_PTR_COMPARE:-0}" = "1" ]; then
            oracle_cases="$oracle_cases pointer_null pointer_equality function_address"
        fi
        if [ "${FOUR_BACKEND_M20_LP64_ABI:-0}" = "1" ]; then
            oracle_cases="$oracle_cases size64_abi"
        fi
        if [ "${FOUR_BACKEND_M21_LP64_ADD:-0}" = "1" ]; then
            oracle_cases="$oracle_cases size64_add"
        fi
        if [ "${FOUR_BACKEND_M22_LP64_CONV:-0}" = "1" ]; then
            oracle_cases="$oracle_cases size64_conv"
        fi
        if [ "${FOUR_BACKEND_M23_LP64_COMPARE:-0}" = "1" ]; then
            oracle_cases="$oracle_cases size64_compare"
        fi
        if [ "${FOUR_BACKEND_M24_INDIRECT_CALL:-0}" = "1" ]; then
            oracle_cases="$oracle_cases indirect_hooks"
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
        if [ "$testcase" = "large_frame" ]; then
            # Verify the *actual* large-frame register adjust and, on
            # LoongArch, an out-of-imm12 SP-relative memory operation;
            # a fake under-2KiB smoke must not satisfy the M7 gate.
            if [ "$target" = "aarch64" ]; then
                grep -Fq 'sub sp, sp, x12' "$w/$target.$testcase.s"
                grep -Fq 'add sp, sp, x12' "$w/$target.$testcase.s"
            else
                grep -Fq 'sub.d $sp, $sp, $t3' "$w/$target.$testcase.s"
                grep -Fq 'add.d $sp, $sp, $t3' "$w/$target.$testcase.s"
                grep -Fq 'add.d $t3, $sp, $t3' "$w/$target.$testcase.s"
            fi
            echo "FOUR_BACKEND_M7_FRAME_ENCODING=PASS target=$target register_SP_adjust=1 large_offset=1"
        fi
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
            bit_xor|bit_and|bit_or|bit_unsigned)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            u8_index|s8_index|u16_index|s16_index)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            size64_abi|size64_add|size64_conv|size64_compare|indirect_hooks|pointer_null|pointer_equality|function_address)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            vararg_int|vararg_pointer)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            record_zero|record_hooks|record_padding)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            u8_return|s8_return|u8_external|u8_internal)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            shift_left|shift_unsigned|shift_signed|shift_byte|shift_count)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            u8_wrap|u8_sub|u8_mul|u16_wrap|s8_add)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            umod|smod|udiv|sdiv)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            unsigned_less|unsigned_add|unsigned_index)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            ubyte_read|sbyte_read|byte_write|ushort_read|short_read|uchar_conv|ushort_conv)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
            large_frame|local_array)
                "$gcc" -static "$w/$target.$testcase.o" "$w/driver_$testcase.c" -o "$w/$target.$testcase.elf" ;;
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
    if [ "${FOUR_BACKEND_M7_FRAME:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M7_FRAME=PASS architectures=2 executable_QEMU_cases=18 large_frame_and_local_array_cases=4 GNU_independent_oracles=14"
    fi
    if [ "${FOUR_BACKEND_M8_NARROW:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M8_NARROW=PASS architectures=2 executable_QEMU_cases=28 signed_unsigned_byte_short_cases=14 GNU_independent_oracles=24"
    fi
    if [ "${FOUR_BACKEND_M9_UNSIGNED:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M9_UNSIGNED=PASS architectures=2 executable_QEMU_cases=20 unsigned_compare_arithmetic_index_cases=6 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M10_MOD:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M10_MOD=PASS architectures=2 executable_QEMU_cases=22 signed_unsigned_div_mod_cases=8 GNU_independent_oracles=18"
    fi
    if [ "${FOUR_BACKEND_M11_INDEX:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M11_INDEX=PASS architectures=2 executable_QEMU_cases=22 signed_unsigned_byte_halfword_index_cases=8 GNU_independent_oracles=18"
    fi
    if [ "${FOUR_BACKEND_M12_BITWISE:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M12_BITWISE=PASS architectures=2 executable_QEMU_cases=22 signed_unsigned_bitwise_cases=8 GNU_independent_oracles=18"
    fi
    if [ "${FOUR_BACKEND_M13_NARROW_ARITH:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M13_NARROW_ARITH=PASS architectures=2 executable_QEMU_cases=24 narrow_wrap_arithmetic_cases=10 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M14_SHIFT:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M14_SHIFT=PASS architectures=2 executable_QEMU_cases=24 signed_unsigned_shift_cases=10 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M15_SCALAR_RETURN:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M15_SCALAR_RETURN=PASS architectures=2 executable_QEMU_cases=22 narrow_return_call_cases=8 GNU_independent_oracles=14"
    fi
    if [ "${FOUR_BACKEND_M17_RECORD:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M17_RECORD=PASS architectures=2 executable_QEMU_cases=20 record_aggregate_relocation_cases=6 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M18_VARARGS:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M18_VARARGS=PASS architectures=2 executable_QEMU_cases=18 integer_pointer_variadic_cases=4 GNU_independent_oracles=14"
    fi
    if [ "${FOUR_BACKEND_M19_PTR_COMPARE:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M19_PTR_COMPARE=PASS architectures=2 executable_QEMU_cases=20 pointer_null_equal_function_address_cases=6 GNU_independent_oracles=16"
    fi
    if [ "${FOUR_BACKEND_M20_LP64_ABI:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M20_LP64_ABI=PASS architectures=2 GNU_QEMU_diff=2 high_32_bits_preserved=true scalar_memory=true"
    fi
    if [ "${FOUR_BACKEND_M21_LP64_ADD:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M21_LP64_ADD=PASS architectures=2 GNU_QEMU_diff=2 high_32_bit_add_sub=true signed_long=true"
    fi
    if [ "${FOUR_BACKEND_M22_LP64_CONV:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M22_LP64_CONV=PASS architectures=2 GNU_QEMU_diff=2 signed_unsigned_extend=true truncate64=true"
    fi
    if [ "${FOUR_BACKEND_M23_LP64_COMPARE:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M23_LP64_COMPARE=PASS architectures=2 GNU_QEMU_diff=2 unsigned_signed_relations=true high_half_equality=true zero_predicate=true"
    fi
    if [ "${FOUR_BACKEND_M24_INDIRECT_CALL:-0}" = "1" ]; then
        echo "FOUR_BACKEND_M24_INDIRECT_CALL=PASS architectures=2 GNU_QEMU_diff=2 function_pointer_ABI=true LP64_argument=true void_callback=true"
    fi
else
    echo "FOUR_BACKEND_M1=PASS architectures=2 executable_QEMU_cases=8 GCC_independent_oracles=4"
fi
