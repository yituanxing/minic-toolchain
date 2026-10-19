#!/bin/sh
# Real SysV x86-64 one-INTEGER-eightbyte ABI oracle (GCC ↔ MiniC).
# No special user source edits, no MiniAS/MiniLD substitution. Fail closed.
set -eu
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo "SKIP one-GP SysV test requires x86-64 host"; exit 0;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/mini.i" <<'EOF'
struct Ipv4 { unsigned int bits; };
typedef union SockArg { void *p; char *alias; } SockArg __attribute__((transparent_union));
extern int host_ipv4(struct Ipv4);
extern int host_sock(SockArg);
extern int host_spill(long, long, long, long, long, long, struct Ipv4);
extern int host_vunion(int, ...);
extern int host_incoming(void);
int minic_ipv4(struct Ipv4 v) { return v.bits == 0x12345678U ? 10 : 0; }
int minic_sock(SockArg v) { return v.p == (void *)0 ? 10 : 0; }
int main(void) {
    struct Ipv4 addr;
    SockArg arg;
    addr.bits = 0x12345678U;
    arg.p = (void *)0;
    if (host_ipv4(addr) != 10) return 1;
    if (host_sock((void *)0) != 10) return 2;
    if (host_spill(1L, 2L, 3L, 4L, 5L, 6L, addr) != 10) return 3;
    if (host_vunion(3, arg) != 10) return 4;
    if (host_incoming() != 20) return 5;
    return 42;
}
EOF
cat > "$work/host.c" <<'EOF'
#include <stdarg.h>
struct Ipv4 { unsigned int bits; };
typedef union SockArg { void *p; char *alias; } SockArg __attribute__((transparent_union));
extern int minic_ipv4(struct Ipv4);
extern int minic_sock(SockArg);
int host_ipv4(struct Ipv4 v) { return v.bits == 0x12345678U ? 10 : 0; }
int host_sock(SockArg v) { return v.p == (void *)0 ? 10 : 0; }
int host_spill(long a, long b, long c, long d, long e, long f, struct Ipv4 v) {
    return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6 &&
        v.bits == 0x12345678U ? 10 : 0;
}
int host_vunion(int tag, ...) {
    va_list ap;
    SockArg arg;
    va_start(ap, tag);
    arg = va_arg(ap, SockArg);
    va_end(ap);
    return tag == 3 && arg.p == (void *)0 ? 10 : 0;
}
int host_incoming(void) {
    struct Ipv4 a = {0x12345678U};
    return minic_ipv4(a) + minic_sock((void *)0);
}
EOF
"$MINIC_CC" --target=x86_64-linux -S "$work/mini.i" -o "$work/mini.s"
"$HOST_CC" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$HOST_CC" -std=gnu11 -c "$work/host.c" -o "$work/host.o"
"$HOST_CC" -no-pie "$work/mini.o" "$work/host.o" -o "$work/mini.bin"
"$HOST_CC" -std=gnu11 -no-pie -x c "$work/mini.i" "$work/host.c" -o "$work/gcc.bin"
set +e
"$work/mini.bin"
mini_rc=$?
"$work/gcc.bin"
gcc_rc=$?
set -e
if test "$mini_rc" -ne 42 || test "$gcc_rc" -ne 42; then
    echo "X86_ONE_GP_SYSV=FAIL MiniC_exit=$mini_rc GNU_exit=$gcc_rc" >&2
    exit 1
fi
echo "X86_ONE_GP_SYSV=PASS gcc_to_minic=1 minic_to_gcc=1 transparent_union=1 fixed_record=1 variadic_union=1 spilled_record=1"
