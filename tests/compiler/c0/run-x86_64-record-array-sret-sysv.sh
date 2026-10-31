#!/usr/bin/env bash
# Original GNU SysV AMD64 ↔ MiniC ABI differential: nested records, arrays,
# empty GNU records, MEMORY-class hidden sret and stack-passed arguments.
set -Eeuo pipefail
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo 'SKIP x86_64 ABI host required'; exit 0;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/mini.i" <<'SOURCE'
struct Empty {};
struct One { unsigned int val[2]; };
struct Two { unsigned int val[4]; };
struct Big { unsigned long a, b, c; unsigned char tail[5]; };
struct RqFlags { unsigned long flags; struct Empty cookie; };
struct Guard { unsigned long lock; struct RqFlags rf; };
extern struct Guard gcc_nested(unsigned long);
extern struct Big gcc_make(long);
extern int gcc_take(long, long, long, long, long, long, struct Big);
extern int gcc_empty(struct Empty, long);
extern struct One gcc_one(struct One);
extern struct Two gcc_two(struct Two);
struct Big minic_make(long x) {
    struct Big result;
    result.a = (unsigned long)x;
    result.b = 17UL;
    result.c = 29UL;
    result.tail[0] = 3;
    result.tail[1] = 4;
    result.tail[2] = 5;
    result.tail[3] = 6;
    result.tail[4] = 7;
    return result;
}
int minic_take(struct Big value, long x) {
    return value.a == 11UL && value.b == 17UL &&
        value.c == 29UL && value.tail[4] == 7 && x == 9;
}
int minic_empty(struct Empty ignored, long value) { return value == 73; }
struct One minic_one(struct One value) {
    value.val[0] += 11U;
    value.val[1] += 13U;
    return value;
}
struct Two minic_two(struct Two value) {
    value.val[0] += 1U;
    value.val[1] += 2U;
    value.val[2] += 3U;
    value.val[3] += 4U;
    return value;
}
struct Guard minic_nested(unsigned long value) {
    struct Guard guard;
    guard.lock = value;
    guard.rf.flags = value + 7UL;
    return guard;
}
int minic_nested_call(void) {
    struct Guard guard = gcc_nested(31UL);
    return guard.lock == 31UL && guard.rf.flags == 38UL;
}
int minic_call_gcc(void) {
    struct Big value = gcc_make(11L);
    struct Empty empty;
    struct One one;
    struct Two two;
    if (value.a != 11UL || value.b != 17UL ||
        value.c != 29UL || value.tail[4] != 7) return 1;
    if (!gcc_take(1L, 2L, 3L, 4L, 5L, 6L, value)) return 2;
    if (!gcc_empty(empty, 73L)) return 3;
    one.val[0] = 3U; one.val[1] = 5U;
    one = gcc_one(one);
    if (one.val[0] != 14U || one.val[1] != 18U) return 4;
    two.val[0] = 1U; two.val[1] = 2U;
    two.val[2] = 3U; two.val[3] = 4U;
    two = gcc_two(two);
    if (two.val[0] != 2U || two.val[1] != 4U ||
        two.val[2] != 6U || two.val[3] != 8U) return 5;
    if (!minic_nested_call()) return 6;
    return 42;
}
SOURCE
cat > "$work/host.c" <<'SOURCE'
struct Empty {};
struct One { unsigned int val[2]; };
struct Two { unsigned int val[4]; };
struct Big { unsigned long a, b, c; unsigned char tail[5]; };
struct RqFlags { unsigned long flags; struct Empty cookie; };
struct Guard { unsigned long lock; struct RqFlags rf; };
extern struct Guard minic_nested(unsigned long);
extern int minic_nested_call(void);
extern struct Big minic_make(long);
extern int minic_take(struct Big, long);
extern int minic_empty(struct Empty, long);
extern struct One minic_one(struct One);
extern struct Two minic_two(struct Two);
extern int minic_call_gcc(void);
struct Big gcc_make(long x) {
    struct Big v = {0};
    v.a = (unsigned long)x; v.b = 17UL; v.c = 29UL;
    v.tail[0] = 3; v.tail[1] = 4; v.tail[2] = 5;
    v.tail[3] = 6; v.tail[4] = 7;
    return v;
}
int gcc_take(long a, long b, long c, long d, long e, long f, struct Big v) {
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 &&
        v.a == 11 && v.b == 17 && v.c == 29 && v.tail[4] == 7;
}
int gcc_empty(struct Empty ignored, long x) { (void)ignored; return x == 73; }
struct One gcc_one(struct One value) {
    value.val[0] += 11; value.val[1] += 13; return value;
}
struct Two gcc_two(struct Two value) {
    value.val[0] += 1; value.val[1] += 2;
    value.val[2] += 3; value.val[3] += 4; return value;
}
struct Guard gcc_nested(unsigned long value) {
    struct Guard guard;
    guard.lock = value;
    guard.rf.flags = value + 7UL;
    return guard;
}
int main(void) {
    struct Big b = minic_make(11L);
    struct Empty empty;
    struct One one = {{3U, 5U}};
    struct Two two = {{1U, 2U, 3U, 4U}};
    if (b.a != 11 || b.b != 17 || b.c != 29 || b.tail[4] != 7) return 10;
    if (!minic_take(b, 9L) || !minic_empty(empty, 73L)) return 11;
    one = minic_one(one);
    two = minic_two(two);
    if (one.val[0] != 14U || one.val[1] != 18U ||
        two.val[0] != 2U || two.val[1] != 4U ||
        two.val[2] != 6U || two.val[3] != 8U) return 12;
    {
        struct Guard guard = minic_nested(31UL);
        if (guard.lock != 31UL || guard.rf.flags != 38UL ||
            !minic_nested_call()) return 13;
    }
    return minic_call_gcc();
}
SOURCE
"$MINIC_CC" --target=x86_64-linux -S "$work/mini.i" -o "$work/mini.s"
"$HOST_CC" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$HOST_CC" -std=gnu11 -c "$work/host.c" -o "$work/host.o"
"$HOST_CC" -no-pie "$work/mini.o" "$work/host.o" -o "$work/minic.out"
"$HOST_CC" -std=gnu11 -no-pie -x c "$work/mini.i" "$work/host.c" -o "$work/gnu.out"
set +e
"$work/minic.out"; mini_rc=$?
"$work/gnu.out"; gnu_rc=$?
set -e
if [[ $mini_rc != 42 || $gnu_rc != 42 ]]; then
    echo "X86_RECORD_ARRAY_SRET_ABI=FAIL minic=$mini_rc gnu=$gnu_rc" >&2
    exit 1
fi
echo 'X86_RECORD_ARRAY_SRET_ABI=PASS one_gp_array two_gp_array empty_record nested_empty_record byvalue_stack hidden_sret direct_call GCC↔MiniC'
