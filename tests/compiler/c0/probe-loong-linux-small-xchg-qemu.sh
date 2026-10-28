#!/usr/bin/env bash
set -Eeuo pipefail

minic="${1:?pass path to minic-cc}"
test -x "$minic"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
source_file="$here/loong-linux-small-xchg.c"
cc -E -P -x c "$source_file" -o "$work/xchg.i"

# The canonical kernel template is parsed as C, transported as six typed
# Core ASM operands, then emitted using LoongArch's physical LL.W / SC.W.
"$minic" --target=loongarch64-linux -S "$work/xchg.i" -o "$work/mini.s"
test -s "$work/mini.s"
grep -Fq 'll.w $t5, $t4, 0' "$work/mini.s"
grep -Fq 'andn $t6, $t5, $t0' "$work/mini.s"
grep -Fq 'or $t6, $t6, $t1' "$work/mini.s"
grep -Fq 'sc.w $t6, $t7, 0' "$work/mini.s"
grep -Fq 'beqz $t6, 1b' "$work/mini.s"
grep -Fq 'st.w $t5, $t7, 0' "$work/mini.s"
grep -Fq 'st.w $t6, $t7, 0' "$work/mini.s"
if [[ "$(grep -Fc 'll.w $t5, $t4, 0' "$work/mini.s")" != 1 ]]; then
    echo 'LoongArch subword helper must retain a single reusable LL/SC engine' >&2
    exit 1
fi

# Each rejected mutation must actually alter the original C fixture; none
# may trick Common Core into treating physical LL/SC as generic opaque ASM.
while IFS='|' read -r id old replacement; do
    test -n "$id" || continue
    sed "s/$old/$replacement/g" "$work/xchg.i" > "$work/$id.i"
    if cmp -s "$work/xchg.i" "$work/$id.i"; then
        echo "invalid small-xchg negative fixture: $id" >&2; exit 1
    fi
    if "$minic" --target=loongarch64-linux -S "$work/$id.i" \
        -o "$work/$id.s" >"$work/$id.log" 2>&1; then
        echo "unsafe small-xchg contract accepted: $id" >&2; exit 1
    fi
    test ! -e "$work/$id.s"
done <<'CASES'
wrong-load|ll.w|ld.w
wrong-store|sc.w|st.w
wrong-op|andn|xor
wrong-retry|beqz|bnez
wrong-write-constraint|=ZC|=m
wrong-read-constraint|"ZC"|"m"
wrong-mask-constraint|"Jr"|"r"
wrong-memory-clobber|: "memory"|: "cc"
wrong-memory-order|%0, %3|%0, %2
wrong-mask-index|%z4|%z5
wrong-retry-label|%1, 1b|%1, 2b
wrong-output-early-clobber|"=&r"|"=r"
wrong-output-memory-class|"=ZC"|"+ZC"
wrong-input-width|u32 old32|unsigned long old32
wrong-asm-volatility|__asm__ __volatile__|__asm__
CASES

if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]]; then
    echo 'LOONG_LINUX_SMALL_XCHG=PASS widths=8/16 GNU_QEMU=pending negatives=15'
    exit 0
fi

gcc=loongarch64-linux-gnu-gcc-13
command -v "$gcc" >/dev/null
command -v qemu-loongarch64 >/dev/null
"$gcc" -static -O0 -x c "$source_file" -o "$work/gnu.elf"
"$gcc" -static -x assembler "$work/mini.s" -o "$work/mini.elf"
for compiler in gnu mini; do
    code=0
    timeout 45s qemu-loongarch64 "$work/$compiler.elf" || code=$?
    if [[ "$code" != 42 ]]; then
        echo "LoongArch small-xchg $compiler exit=$code, expected 42" >&2
        exit 1
    fi
done
echo 'LOONG_LINUX_SMALL_XCHG=PASS widths=8/16 GNU_QEMU_42=2 negatives=15'
