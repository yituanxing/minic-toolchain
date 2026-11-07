#!/usr/bin/env bash
# A genuine 4-byte Linux enum is a legal GS/percpu XADD memory operand.
# Keep the source admission strict about complete enum storage width.
set -Eeuo pipefail
cc=${1:?MiniC compiler}
test -x "$cc"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

cat >"$d/good.i" <<'SRC'
enum nmi_states { NMI_IDLE = 0, NMI_HANDLING = 1 };
unsigned int mini_xadd(volatile enum nmi_states *p, unsigned int value) {
    asm volatile("xaddl %0, %%gs:%1"
                 : "+r"(value), "+m"(*p) : : "memory");
    return value;
}
SRC
sed 's/mini_xadd/gnu_xadd/' "$d/good.i" >"$d/oracle.i"

"$cc" --target=x86_64-linux -S "$d/good.i" -o "$d/good.s"
gcc -c -x assembler "$d/good.s" -o "$d/good.o"
gcc -O0 -c -x c "$d/oracle.i" -o "$d/oracle.o"
grep -Fq 'xaddl %r10d, %gs:(%r11)' "$d/good.s"
readelf -h "$d/good.o" | grep -Fq 'Advanced Micro Devices X86-64'

cat >"$d/driver.c" <<'SRC'
#include <sys/syscall.h>
#include <unistd.h>
#define ARCH_SET_GS 0x1001
enum nmi_states { NMI_IDLE = 0, NMI_HANDLING = 1 };
extern unsigned int mini_xadd(volatile enum nmi_states *, unsigned int);
extern unsigned int gnu_xadd(volatile enum nmi_states *, unsigned int);
int main(void) {
    volatile enum nmi_states state = NMI_HANDLING;
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)&state)) return 2;
    state = NMI_HANDLING;
    unsigned int mini_old = mini_xadd((volatile enum nmi_states *)0, 17U);
    unsigned int mini_sum = (unsigned int)state;
    state = NMI_HANDLING;
    unsigned int gnu_old = gnu_xadd((volatile enum nmi_states *)0, 17U);
    return mini_old == gnu_old && mini_old == 1U &&
           mini_sum == (unsigned int)state && mini_sum == 18U ? 0 : 3;
}
SRC
gcc -O0 -no-pie "$d/driver.c" "$d/good.o" "$d/oracle.o" -o "$d/compare"
"$d/compare"

# The same enum object cannot be used by a 64-bit XADD just because the
# register is a valid 64-bit integer. The machine width must match +m.
cat >"$d/bad-width.i" <<'SRC'
enum nmi_states { NMI_IDLE = 0, NMI_HANDLING = 1 };
unsigned long bad_width(volatile enum nmi_states *p, unsigned long value) {
    asm volatile("xaddq %0, %%gs:%1"
                 : "+r"(value), "+m"(*p) : : "memory");
    return value;
}
SRC

# GNU permits a forward enum declaration, but not an incomplete 4-byte
# memory object. Frontend or target source qualification must fail closed.
cat >"$d/bad-incomplete.i" <<'SRC'
enum nmi_states;
unsigned int bad_incomplete(volatile enum nmi_states *p, unsigned int value) {
    asm volatile("xaddl %0, %%gs:%1"
                 : "+r"(value), "+m"(*p) : : "memory");
    return value;
}
SRC
for bad in bad-width bad-incomplete; do
    if "$cc" --target=x86_64-linux -S "$d/$bad.i" -o "$d/$bad.s" \
            >"$d/$bad.log" 2>&1; then
        echo "FAIL invalid GS enum XADD accepted: $bad" >&2
        exit 1
    fi
    test ! -e "$d/$bad.s"
done
echo 'MINIC_X86_GS_XADD_ENUM=PASS GNU_native=1 negative=2'
