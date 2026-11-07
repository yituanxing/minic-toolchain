#!/bin/sh
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
host_cc=${HOST_CC:-cc}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

"$host_cc" -E -P \
    "$source_root/tests/target/x86_64_linux_vdso_pvclock_syscall.c" \
    -o "$workdir/vdso.i"
"$minic_cc" --target=x86_64-linux -S "$workdir/vdso.i" \
    -o "$workdir/vdso.s"
as --64 "$workdir/vdso.s" -o "$workdir/vdso.o"
objdump -d "$workdir/vdso.o" > "$workdir/vdso.dis"
if [ "$(grep -Ec 'mul[[:space:]]+%r10' "$workdir/vdso.dis")" -lt 2 ] ||
   [ "$(grep -Ec 'shrd[[:space:]]+\$0x20,%rdx,%rax' "$workdir/vdso.dis")" -lt 2 ] ||
   [ "$(grep -Ec 'syscall' "$workdir/vdso.dis")" -lt 2 ]; then
    printf 'FAIL: missing RAX/RDX pvclock funnel shift or vDSO syscall bytes\n' >&2
    exit 1
fi
"$host_cc" -no-pie "$workdir/vdso.o" -o "$workdir/vdso"
"$workdir/vdso"

sed 's/shrd \$32/shrd \$31/g' "$workdir/vdso.i" > "$workdir/bad-shift.i"
if "$minic_cc" --target=x86_64-linux -S "$workdir/bad-shift.i" \
    -o "$workdir/bad-shift.s" > "$workdir/bad-shift.log" 2>&1; then
    printf 'FAIL: forged pvclock SHRD shift was accepted\n' >&2
    exit 1
fi
sed 's/"rcx", "r11"/"rcx", "r12"/' "$workdir/vdso.i" \
    > "$workdir/bad-syscall-clobber.i"
if "$minic_cc" --target=x86_64-linux -S "$workdir/bad-syscall-clobber.i" \
    -o "$workdir/bad-syscall-clobber.s" > "$workdir/bad-syscall-clobber.log" 2>&1; then
    printf 'FAIL: forged vDSO syscall clobber was accepted\n' >&2
    exit 1
fi
printf 'PASS: x86 vDSO pvclock MULQ/SHRD, syscall GNU as, runtime and negative contracts\n'
