#!/usr/bin/env bash
# GNU GAS oracle for a user-owned .debug_line and a synthesized .file/.loc unit.
# GNU permits an explicit EMPTY .debug_line, but rejects combining any user
# bytes with a generated line table, regardless of source statement order.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to the exact-head minic-as binary}"
command -v as >/dev/null
command -v readelf >/dev/null
command -v objcopy >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/before.s" <<'ASM'
.section .debug_line
.byte 0x11,0x22,0x33,0x44
.text
.file 1 "probe.c"
.loc 1 10 0
.globl probe
probe:
    nop
ASM

cat >"$work/after.s" <<'ASM'
.text
.file 1 "probe.c"
.loc 1 10 0
.globl probe
probe:
    nop
.section .debug_line
.byte 0x55,0x66
ASM

for case_name in before after; do
    if as --64 -o "$work/gnu-$case_name.o" "$work/$case_name.s" \
            >"$work/gnu-$case_name.out" 2>"$work/gnu-$case_name.err"; then
        echo "GNU unexpectedly allowed user bytes + generated DWARF: $case_name" >&2
        exit 1
    fi
    grep -Fq 'duplicate .debug_line sections' "$work/gnu-$case_name.err"
    test ! -e "$work/gnu-$case_name.o"

    if "$MINIAS" --target=x86_64 -o "$work/mini-$case_name.o" \
            "$work/$case_name.s" >"$work/mini-$case_name.out" \
            2>"$work/mini-$case_name.err"; then
        echo "MiniAS silently combined user bytes + generated DWARF: $case_name" >&2
        exit 1
    fi
    grep -Fq 'duplicate-dwarf-line-section' "$work/mini-$case_name.err"
    test ! -e "$work/mini-$case_name.o"
done

# An explicitly selected but empty Section does not conflict with GAS's
# generated unit.  The two producers can differ in DWARF version/encoding,
# so compare decoded source locations and relocation-section ownership.
cat >"$work/empty.s" <<'ASM'
.section .debug_line
.text
.file 1 "probe.c"
.loc 1 10 0
.globl probe
probe:
    nop
ASM
as --64 -o "$work/gnu-empty.o" "$work/empty.s"
"$MINIAS" --target=x86_64 -o "$work/mini-empty.o" "$work/empty.s"
for producer in gnu mini; do
    readelf --debug-dump=decodedline "$work/$producer-empty.o" \
        >"$work/$producer-empty.line"
    grep -Eq 'probe\.c[[:space:]]+10[[:space:]]+' \
        "$work/$producer-empty.line"
    readelf -rW "$work/$producer-empty.o" \
        >"$work/$producer-empty.reloc"
    grep -Eq 'Relocation section .*\.rela\.debug_line' \
        "$work/$producer-empty.reloc"
done

# With no .loc, the user-created Section belongs entirely to the source.
# Its bytes must be preserved, and no synthetic DWARF line unit may appear.
cat >"$work/user-only.s" <<'ASM'
.section .debug_line
.byte 0x11,0x22,0x33,0x44
.text
.file 1 "probe.c"
.globl probe
probe:
    nop
ASM
as --64 -o "$work/gnu-user-only.o" "$work/user-only.s"
"$MINIAS" --target=x86_64 -o "$work/mini-user-only.o" \
    "$work/user-only.s"
for producer in gnu mini; do
    objcopy -j .debug_line -O binary "$work/$producer-user-only.o" \
        "$work/$producer-user-only.bin"
done
cmp "$work/gnu-user-only.bin" "$work/mini-user-only.bin"

echo 'MINIAS_DEBUG_LINE_MIXED_SECTION_GNU=PASS invalid_orderings=2 empty_section=1 source_owned=1'
