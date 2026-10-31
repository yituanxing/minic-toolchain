#!/usr/bin/env bash
set -Eeuo pipefail
minic="${1:?expected minic-cc}"
test -x "$minic"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
source="$root/tests/compiler/c0/loong-linux-named-store-mb.c"
cc -E -P -x c "$source" -o "$work/original.i"

"$minic" --target=loongarch64-linux -S "$work/original.i" -o "$work/mini.s"
test -s "$work/mini.s"
grep -Fq 'amswap_db.w $t2, $t0, $t1' "$work/mini.s"
grep -Fq 'amswap_db.d $t2, $t0, $t1' "$work/mini.s"
grep -Fq 'st.d $t2, $t1, 0' "$work/mini.s"
if [[ "$(grep -Fc 'amswap_db.' "$work/mini.s")" != 3 ]];then
    echo 'LoongArch Linux store-mb must emit exactly three hardware AMDB stores' >&2
    exit 1
fi

# All mutations preserve a plausible GNU inline-ASM surface but violate
# the actual kernel ISA, register, memory or ordering contract.
while IFS='|' read -r label before after; do
    test -n "$label" || continue
    sed "s/$before/$after/g" "$work/original.i" > "$work/$label.i"
    if cmp -s "$work/original.i" "$work/$label.i";then
        echo "invalid named AMDB rejection fixture: $label" >&2;exit 1
    fi
    if "$minic" --target=loongarch64-linux -S "$work/$label.i" \
        -o "$work/$label.s" >"$work/$label.log" 2>&1;then
        echo "unsafe GNU Linux named AMDB accepted: $label" >&2;exit 1
    fi
    test ! -e "$work/$label.s"
done <<'CASES'
wrong-w-op|amswap_db.w|amxor_db.w
wrong-d-op|amswap_db.d|amswap_db.w
wrong-value-constraint|"r" (value)|"Jr" (value)
wrong-memory-constraint|"+ZB"|"+m"
wrong-output-constraint|"=&r"|"=r"
wrong-clobber|: );|: "memory");
wrong-address|%\[mem\]|%[val]
wrong-input|%\[val\]|%[tmp]
wrong-volatile|__asm__ __volatile__|__asm__
wrong-output-width|unsigned long tmp|u32 tmp
CASES

if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]];then
    echo 'LOONG_NAMED_STORE_MB=PASS widths=32/64 mode=source negatives=10'
    exit 0
fi

gcc=loongarch64-linux-gnu-gcc-13
command -v "$gcc" >/dev/null
command -v qemu-loongarch64 >/dev/null
"$gcc" -static -O0 -x c "$source" -o "$work/gnu.elf"
"$gcc" -static -x assembler "$work/mini.s" -o "$work/mini.elf"
for compiler in gnu mini; do
    rc=0
    timeout 45s qemu-loongarch64 "$work/$compiler.elf" || rc=$?
    if [[ "$rc" != 42 ]];then
        echo "LoongArch named store-mb $compiler exit=$rc expected=42" >&2
        exit 1
    fi
done
echo 'LOONG_NAMED_STORE_MB=PASS widths=32/64 GNU_QEMU_42=2 negatives=10'
