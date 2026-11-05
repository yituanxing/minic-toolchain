#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
: "${MINIOBJCOPY:?Set MINIOBJCOPY to the exact-head MiniObjcopy binary}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/member.s" <<'SRC'
.section .text.grouped,"axG",@progbits,sig_group,comdat
.globl grouped_func
.type grouped_func,@function
grouped_func:
    ret
.size grouped_func,.-grouped_func
.section .rodata.grouped,"aG",@progbits,sig_group,comdat
.long grouped_func
SRC

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/member.s"
as --64 -o "$work/gnu.o" "$work/member.s"

for producer in mini gnu; do
    if [[ "$producer" == mini ]]; then copy=("$MINIOBJCOPY");
    else copy=(objcopy); fi
    "${copy[@]}" --prefix-symbols=__re_ --remove-section=.rodata.grouped \
        "$work/$producer.o" "$work/$producer-one.o"
    "${copy[@]}" --prefix-symbols=__re_ --remove-section=.rodata.grouped \
        --remove-section=.text.grouped \
        "$work/$producer.o" "$work/$producer-none.o"
    readelf --section-groups "$work/$producer-one.o" >"$work/$producer-one.groups"
    readelf --section-groups "$work/$producer-none.o" >"$work/$producer-none.groups"
done

python3 - "$work" <<'PY'
import pathlib, re, sys
root=pathlib.Path(sys.argv[1])
for producer in ('mini','gnu'):
    one=(root/f'{producer}-one.groups').read_text()
    none=(root/f'{producer}-none.groups').read_text()
    if not re.search(r'COMDAT group section.*\[__re_sig_group\]',one):
        raise SystemExit(f'{producer}: signature missing after member removal\n{one}')
    members=set(re.findall(r'\[\s*\d+\]\s+(\.\S+)\s*$',one,re.M))
    if members != {'.text.grouped'}:
        raise SystemExit(f'{producer}: invalid member table {members}\n{one}')
    if 'COMDAT group section' in none or '.text.grouped' in none:
        raise SystemExit(f'{producer}: empty COMDAT group retained\n{none}')
print('MINIOBJCOPY_COMDAT_REWRITE=PASS remove_member=1 empty_group=removed GNU=equivalent')
PY
