#!/bin/sh
# Linux 6.6.143 arch/loongarch/vdso/vgettimeofday.c fallback wrappers.
# Preserve the original target-qualified a7/a0/a1 ABI, the nine t-register
# clobbers and the compiler memory barrier; no generic syscall text escape.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/vdso.i" <<'SRC'
struct old_tv { long seconds; long micros; };
struct zone { int west; int dst; };
struct timespec { long seconds; long nanos; };

long vdso_gettimeofday_fallback(struct old_tv *_tv, struct zone *_tz) {
    register struct old_tv *tv asm("a0") = _tv;
    register struct zone *tz asm("a1") = _tz;
    register long nr asm("a7") = 169;
    register long ret asm("a0");
    asm volatile("       syscall 0\n"
        : "+r"(ret) : "r"(nr), "r"(tv), "r"(tz)
        : "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8", "memory");
    return ret;
}

int vdso_clock_getres_fallback(int _clockid, struct timespec *_ts) {
    register int clockid asm("a0") = _clockid;
    register struct timespec *ts asm("a1") = _ts;
    register long nr asm("a7") = 114;
    register long ret asm("a0");
    asm volatile("       syscall 0\n"
        : "+r"(ret) : "r"(nr), "r"(clockid), "r"(ts)
        : "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8", "memory");
    return (int)ret;
}
SRC
"$MINIC_CC" --target=loongarch64-linux -S \
    "$work/vdso.i" -o "$work/vdso.s"
test "$(grep -Ec '^[[:space:]]+syscall[[:space:]]+0' "$work/vdso.s")" -eq 2
test "$(grep -Ec '^[[:space:]]+ld\.d[[:space:]]+\$a7,' "$work/vdso.s")" -eq 2
grep -Eq '^[[:space:]]+ld\.d[[:space:]]+\$a0,' "$work/vdso.s"
grep -Eq '^[[:space:]]+ld\.w[[:space:]]+\$a0,' "$work/vdso.s"
test "$(grep -Ec '^[[:space:]]+ld\.d[[:space:]]+\$a1,' "$work/vdso.s")" -eq 2
test "$(grep -Ec '^[[:space:]]+st\.d[[:space:]]+\$a0,' "$work/vdso.s")" -ge 2
echo 'MINIC_LOONG_VDSO_SYSCALL_SHAPE=PASS a7_number a0_arg a1_arg a0_return count=2'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/vdso.s" -o "$work/vdso.o"
    echo 'MINIC_LOONG_VDSO_SYSCALL_GNU_AS=PASS'
else
    echo 'MINIC_LOONG_VDSO_SYSCALL_GNU_AS=SKIP missing Loong GNU assembler'
fi

# The literal syscall mnemonic alone does not grant the privilege to bind
# arbitrary operand registers, omit hardware clobbers or change the opcode.
sed 's/asm("a7")/asm("a6")/g' "$work/vdso.i" >"$work/wrong-register.i"
sed 's/"\$t8", "memory"/"\$t9", "memory"/g' \
    "$work/vdso.i" >"$work/wrong-clobber.i"
sed 's/syscall 0/syscall 1/g' "$work/vdso.i" >"$work/wrong-opcode.i"
for name in wrong-register wrong-clobber wrong-opcode; do
    if "$MINIC_CC" --target=loongarch64-linux -S \
        "$work/$name.i" -o "$work/$name.s" \
        >/dev/null 2>"$work/$name.err"; then
        echo "MINIC_LOONG_VDSO_SYSCALL=FAIL $name admitted" >&2
        exit 1
    fi
    test ! -e "$work/$name.s"
done
echo 'MINIC_LOONG_VDSO_SYSCALL_NEGATIVE=PASS wrong_register wrong_clobber wrong_opcode'
