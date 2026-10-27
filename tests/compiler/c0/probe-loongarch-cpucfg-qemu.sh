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
# Other targets cannot silently accept a machine instruction they do not own.
if "$cc" --target=x86_64-linux -S "$work/original.i" \
    -o "$work/invalid.s" >"$work/invalid.log" 2>&1; then
    echo 'CPUCFG unexpectedly supported on x86-64' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'LOONGARCH_CPUCFG_GNU_QEMU=PASS original_builtin=1 physical_cpucfg=1 negative_x86=1'
