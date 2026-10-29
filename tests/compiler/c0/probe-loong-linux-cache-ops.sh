#!/usr/bin/env bash
# Preserve the actual Linux LoongArch cache-maintenance instruction family.
# CACOP is a privileged hardware cache operation, so user QEMU execution is
# not a meaningful oracle; GNU GAS validates the emitted ISA when installed.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/cache.i" <<'SOURCE'
void flush_cache_line(unsigned long addr) {
    __asm__ __volatile__("\tcacop %0, %1\n" : : "i" ((0x00 | 0x08)), "ZC" (*(unsigned char *)(addr)));
    __asm__ __volatile__("\tcacop %0, %1\n" : : "i" ((0x01 | 0x08)), "ZC" (*(unsigned char *)(addr)));
    __asm__ __volatile__("\tcacop %0, %1\n" : : "i" ((0x02 | 0x08)), "ZC" (*(unsigned char *)(addr)));
    __asm__ __volatile__("\tcacop %0, %1\n" : : "i" ((0x03 | 0x08)), "ZC" (*(unsigned char *)(addr)));
    __asm__ __volatile__("\tcacop %0, %1\n" : : "i" ((0x04 | 0x08)), "ZC" (*(unsigned char *)(addr)));
    __asm__ __volatile__("\tcacop %0, %1\n" : : "i" ((0x05 | 0x08)), "ZC" (*(unsigned char *)(addr)));
    asm volatile("\tibar 0\n"::);
}
SOURCE
"$mini" --target=loongarch64-linux -S "$work/cache.i" -o "$work/cache.s"
for op in 8 9 10 11 12 13; do
    test "$(grep -Fc "cacop $op, \$t0, 0" "$work/cache.s")" -eq 1
done
test "$(grep -Fc 'ibar 0' "$work/cache.s")" -eq 1
test "$(grep -c 'cacop ' "$work/cache.s")" -eq 6

for bad in range runtime constraint type injected ibar; do
    case "$bad" in
        range) cat > "$work/bad.i" <<'SOURCE'
void f(unsigned long addr) { asm volatile("cacop %0, %1"::"i" (7), "ZC" (*(unsigned char*)addr)); }
SOURCE
            ;;
        runtime) cat > "$work/bad.i" <<'SOURCE'
void f(unsigned long addr, int op) { asm volatile("cacop %0, %1"::"i" (op), "ZC" (*(unsigned char*)addr)); }
SOURCE
            ;;
        constraint) cat > "$work/bad.i" <<'SOURCE'
void f(unsigned long addr) { asm volatile("cacop %0, %1"::"i" (8), "r" (addr)); }
SOURCE
            ;;
        type) cat > "$work/bad.i" <<'SOURCE'
void f(unsigned long addr) { asm volatile("cacop %0, %1"::"i" (8), "ZC" (*(unsigned short*)addr)); }
SOURCE
            ;;
        injected) cat > "$work/bad.i" <<'SOURCE'
void f(unsigned long addr) { asm volatile("cacop %0, %1; nop"::"i" (8), "ZC" (*(unsigned char*)addr)); }
SOURCE
            ;;
        ibar) cat > "$work/bad.i" <<'SOURCE'
void f(void) { asm volatile("ibar 0\nnop"::); }
SOURCE
            ;;
    esac
    if "$mini" --target=loongarch64-linux -S "$work/bad.i" \
            -o "$work/bad.s" >"$work/bad.out" 2>&1; then
        printf 'LoongArch unsafe hardware CACOP/IBAR accepted: %s\n' "$bad" >&2
        exit 1
    fi
    test ! -e "$work/bad.s"
done

if [[ "${MINIC_LOONG_CACOP_SOURCE_ONLY:-0}" != 1 ]]; then
    as=${LOONGARCH64_GAS:-loongarch64-linux-gnu-as}
    command -v "$as" >/dev/null
    "$as" -o "$work/cache.o" "$work/cache.s"
fi
printf 'MINIC_LOONGARCH_CACHE=PASS hardware_cacop=6 hardware_ibar=1 negatives=6 source_only=%s\n' \
    "${MINIC_LOONG_CACOP_SOURCE_ONLY:-0}"
