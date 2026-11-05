#!/usr/bin/env bash
# GNU BFD final-link symbol ownership: input STT_SECTION is a relocation
# operand, not an output symbol, and /DISCARD/ locals must not become UND.
set -Eeuo pipefail

MINILD=${MINILD:-build/release/bin/minic-ld}
command -v as >/dev/null
command -v ld >/dev/null
command -v readelf >/dev/null
[[ -x $MINILD ]] || { echo 'MINILD_FINAL_SYMBOLS=FAIL missing_minild' >&2; exit 1; }
work=$(mktemp -d)
cleanup() {
    if [[ ${MINILD_FINAL_SYMBOLS_KEEP:-0} == 1 ]]; then
        printf 'MINILD_FINAL_SYMBOLS_ARTIFACTS=%s\n' "$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

printf '%s\n' \
    '.section .text.start,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '    call target' \
    '    ret' \
    '.section .data.first,"aw",@progbits' \
    'first_local:' \
    '    .quad target' \
    '.section .discard.me,"a",@progbits' \
    'discarded_local:' \
    '    .quad 0x11223344' \
    '.section .note.GNU-stack,"",@progbits' >"$work/first.s"
printf '%s\n' \
    '.section .text.target,"ax",@progbits' \
    '.globl target' \
    'target:' \
    '    ret' \
    '.section .data.second,"aw",@progbits' \
    'second_local:' \
    '    .quad _start' \
    '.section .note.GNU-stack,"",@progbits' >"$work/second.s"
printf '%s\n' \
    'ENTRY(_start)' \
    'SECTIONS {' \
    '  . = 0x400000;' \
    '  .text : { *(.text.start) *(.text.target) }' \
    '  . = ALIGN(0x1000);' \
    '  .data : { *(.data.first) *(.data.second) }' \
    '  /DISCARD/ : { *(.discard.me) *(.note.GNU-stack) }' \
    '}' >"$work/image.lds"

as --64 -o "$work/first.o" "$work/first.s"
as --64 -o "$work/second.o" "$work/second.s"
ld -m elf_x86_64 -X -T "$work/image.lds" -o "$work/gnu.elf" \
    "$work/first.o" "$work/second.o"
"$MINILD" -m elf_x86_64 -X -T "$work/image.lds" -o "$work/mini.elf" \
    "$work/first.o" "$work/second.o"
for producer in gnu mini; do
    readelf -Ws "$work/$producer.elf" |
        awk '$4 == "SECTION" && $5 == "LOCAL" { print $7, $2 }' |
        sort >"$work/$producer.section-symbols"
    readelf -Ws "$work/$producer.elf" |
        awk '$5 == "LOCAL" && $7 == "UND" { ++n } END {print n+0}' \
        >"$work/$producer.local-undefined"
    readelf -Ws "$work/$producer.elf" |
        awk '$8 == "discarded_local" { ++n } END {print n+0}' \
        >"$work/$producer.discarded"
done

diff -u "$work/gnu.section-symbols" "$work/mini.section-symbols"
[[ $(<"$work/gnu.local-undefined") == 1 ]]
[[ $(<"$work/mini.local-undefined") == 1 ]]
[[ $(<"$work/gnu.discarded") == 0 ]]
[[ $(<"$work/mini.discarded") == 0 ]]

# A surviving reference into /DISCARD/ is an invalid link, not permission to
# rewrite the now-removed source local as SHN_UNDEF/zero in the image.
printf '%s\n' \
    '.section .text.start,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '    .quad discarded_local' \
    '.section .discard.me,"a",@progbits' \
    'discarded_local:' \
    '    .quad 0xdeadbeef' >"$work/bad.s"
as --64 -o "$work/bad.o" "$work/bad.s"
if ld -m elf_x86_64 -X -T "$work/image.lds" \
        -o "$work/bad-gnu.elf" "$work/bad.o" >"$work/bad-gnu.log" 2>&1; then
    echo 'MINILD_FINAL_SYMBOLS=FAIL GNU_accepted_discard_reference' >&2
    exit 1
fi
if "$MINILD" -m elf_x86_64 -X -T "$work/image.lds" \
        -o "$work/bad-mini.elf" "$work/bad.o" >"$work/bad-mini.log" 2>&1; then
    echo 'MINILD_FINAL_SYMBOLS=FAIL Mini_accepted_discard_reference' >&2
    exit 1
fi
printf 'MINILD_FINAL_SYMBOLS=PASS mode=ET_EXEC section_symbols=GNU_no_emit discard_local_no_UND=true illegal_live_reference=GNU_Mini_reject\n'
