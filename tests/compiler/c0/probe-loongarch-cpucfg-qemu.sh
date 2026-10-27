#!/usr/bin/env bash
# First production use of the original LoongArch GCC CPUCFG builtin.
# Compare MiniC's real hardware instruction with GCC under native QEMU.
set -Eeuo pipefail
cc=${1:?MiniC compiler required}
gcc=loongarch64-linux-gnu-gcc-13
qemu=qemu-loongarch64
command -v "$gcc" >/dev/null || { echo "missing GNU LoongArch C oracle: $gcc" >&2; exit 1; }
command -v "$qemu" >/dev/null || { echo "missing LoongArch QEMU: $qemu" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/original.i" <<'EOF'
unsigned int minic_probe_cpucfg(unsigned int leaf) {
    return __builtin_loongarch_cpucfg(leaf);
}
EOF
cat > "$work/driver.c" <<'EOF'
extern unsigned int minic_probe_cpucfg(unsigned int);
int main(void) {
    unsigned int actual = minic_probe_cpucfg(0U);
    unsigned int reference = __builtin_loongarch_cpucfg(0U);
    return actual == reference ? 42 : 1;
}
EOF
"$cc" --target=loongarch64-linux -S "$work/original.i" -o "$work/minic.s"
grep -F '  cpucfg $a0, $a0' "$work/minic.s" >/dev/null
"$gcc" -static "$work/driver.c" -x assembler "$work/minic.s" \
    -o "$work/minic.elf"
"$gcc" -static -x c "$work/original.i" "$work/driver.c" \
    -o "$work/gnu.elf"
for target in minic gnu; do
    set +e
    "$qemu" "$work/$target.elf"
    rc=$?
    set -e
    test "$rc" -eq 42 || { echo "CPUCFG $target QEMU returned $rc" >&2; exit 1; }
done
# The original Linux GNU preprocessed corpus declares global TP/SP register
# variables. Check real live register transport through the shared Core
# FIXED_REGISTER_READ and target LP64 machine output with independent GNU
# linker/QEMU, not a parser-only allowance.
cat > "$work/fixed.i" <<'EOF'
register unsigned long minic_live_tp asm("$tp");
register unsigned long minic_live_sp asm("$sp");
unsigned long minic_read_tp(void) { return minic_live_tp; }
unsigned long minic_read_sp(void) { return minic_live_sp; }
EOF
cat > "$work/fixed-driver.c" <<'EOF'
extern unsigned long minic_read_tp(void);
extern unsigned long minic_read_sp(void);
register unsigned long oracle_tp asm("$tp");
int main(void) {
    unsigned long live = minic_read_sp();
    unsigned long local = (unsigned long)&live;
    unsigned long diff = live > local ? live - local : local - live;
    return (minic_read_tp() == oracle_tp && live != 0 && diff < 65536UL)
        ? 42 : 1;
}
EOF
"$cc" --target=loongarch64-linux -S "$work/fixed.i" -o "$work/fixed.s"
grep -F '  move $t0, $tp' "$work/fixed.s" >/dev/null
grep -F '  move $t0, $sp' "$work/fixed.s" >/dev/null
"$gcc" -static "$work/fixed-driver.c" -x assembler "$work/fixed.s" \
    -o "$work/fixed.elf"
set +e
"$qemu" "$work/fixed.elf"
rc=$?
set -e
test "$rc" -eq 42 || { echo "LoongArch fixed TP/SP QEMU failed: $rc" >&2; exit 1; }
printf '%s\n' 'LOONGARCH_FIXED_TP_SP_GNU_QEMU=PASS live_tp=1 live_sp=1 shared_core=1'
# In original Linux 6.6.143 the per-CPU offset is a global register
# variable on the architecturally reserved $r21. Its live value is compared
# against a GNU-compiled independent C reference; do not substitute a global
# ELF object or hardcode zero.
cat > "$work/r21.i" <<'EOF'
register unsigned long minic_percpu_offset asm("$r21");
unsigned long minic_read_percpu(void) { return minic_percpu_offset; }
EOF
cat > "$work/r21-driver.c" <<'EOF'
register unsigned long oracle_percpu asm("$r21");
extern unsigned long minic_read_percpu(void);
int main(void) { return minic_read_percpu() == oracle_percpu ? 42 : 1; }
EOF
"$cc" --target=loongarch64-linux -S "$work/r21.i" -o "$work/r21.s"
grep -F '  move $t0, $r21' "$work/r21.s" >/dev/null
"$gcc" -static "$work/r21-driver.c" -x assembler "$work/r21.s" \
    -o "$work/r21.elf"
set +e
"$qemu" "$work/r21.elf"
rc=$?
set -e
test "$rc" -eq 42 || { echo "LoongArch live r21 QEMU failed: $rc" >&2; exit 1; }
# Local register variables that GCC uses for extended-asm input carriers must
# remain syntactically admissible without inventing a new Core opcode. Their
# normal C value flow still follows Common Core SSA; actual asm register
# binding requires separate machine proof and is NOT implicitly accepted.
cat > "$work/local-regs.i" <<'EOF'
unsigned long minic_local_regs(unsigned long x) {
    register unsigned long t asm("t0") = x + 1UL;
    register unsigned long a asm("a0") = t + 2UL;
    register unsigned long b asm("a1") = a + 3UL;
    register unsigned long c asm("a7") = b + 4UL;
    return c;
}
EOF
cat > "$work/local-driver.c" <<'EOF'
extern unsigned long minic_local_regs(unsigned long);
int main(void) { return minic_local_regs(32UL) == 42UL ? 42 : 1; }
EOF
"$cc" --target=loongarch64-linux -S "$work/local-regs.i" -o "$work/local-regs.s"
"$gcc" -static "$work/local-driver.c" -x assembler "$work/local-regs.s" \
    -o "$work/local-regs.elf"
"$gcc" -static -x c "$work/local-regs.i" "$work/local-driver.c" \
    -o "$work/local-regs.gnu.elf"
for mode in local-regs local-regs.gnu; do
    set +e
    "$qemu" "$work/$mode.elf"
    rc=$?
    set -e
    test "$rc" -eq 42 || { echo "LoongArch local hardreg $mode returned $rc" >&2; exit 1; }
done
cat > "$work/invalid-local.i" <<'EOF'
unsigned long bad(unsigned long x) {
    register unsigned long unsupported asm("t1") = x;
    return unsupported;
}
EOF
if "$cc" --target=loongarch64-linux -S "$work/invalid-local.i" \
    -o "$work/invalid-local.s" >"$work/invalid-local.log" 2>&1; then
    echo 'LoongArch unqualified scratch local was incorrectly admitted' >&2
    exit 1
fi
grep -F 'local fixed register binding is not supported' "$work/invalid-local.log" >/dev/null
cat > "$work/invalid-global.i" <<'EOF'
register unsigned long unsupported asm("$r20");
unsigned long bad(void) { return unsupported; }
EOF
if "$cc" --target=loongarch64-linux -S "$work/invalid-global.i" \
    -o "$work/invalid-global.s" >"$work/invalid-global.log" 2>&1; then
    echo 'LoongArch unqualified global register was incorrectly admitted' >&2
    exit 1
fi
grep -F 'fixed register binding is not supported' "$work/invalid-global.log" >/dev/null
printf '%s\n' 'LOONGARCH_LINUX_FIXED_HARDREG_GNU_QEMU=PASS global_r21=1 four_local_names=1 rejected_others=1'
cat > "$work/invalid-fixed.i" <<'EOF'
register unsigned long invalid_machine_binding asm("$a0");
unsigned long bad(void) { return invalid_machine_binding; }
EOF
if "$cc" --target=loongarch64-linux -S "$work/invalid-fixed.i" \
    -o "$work/invalid-fixed.s" >"$work/invalid-fixed.log" 2>&1; then
    echo 'Unqualified LoongArch fixed register incorrectly admitted' >&2
    exit 1
fi
# Other targets cannot silently accept a machine instruction they do not own.
if "$cc" --target=x86_64-linux -S "$work/original.i" \
    -o "$work/invalid.s" >"$work/invalid.log" 2>&1; then
    echo 'CPUCFG unexpectedly supported on x86-64' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'LOONGARCH_CPUCFG_GNU_QEMU=PASS original_builtin=1 physical_cpucfg=1 negative_x86=1'
