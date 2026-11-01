#!/usr/bin/env bash
# One lexical collector walk, independent macro/repeat stack policies.
set -Eeuo pipefail
: "$MINIAS"
for tool in as objcopy cmp; do command -v "$tool" >/dev/null; done
python3 - <<'PY'
from pathlib import Path
s=Path('assembler/src/source_expansion.c').read_text()
if (s.count('find_macro_top_level_separator(')!=5 or
    'MINIAS_MACRO_SEPARATOR_COMMA|MINIAS_MACRO_SEPARATOR_SPACE' not in s):
    raise SystemExit('macro declaration/invocation delimiter scanner duplicated')
tokens=s[s.index('static size_t split_macro_tokens('):
         s.index('static bool macro_has_top_level_comma(')]
arguments=s[s.index('static size_t split_macro_arguments('):
            s.index('static MiniAsMacro *minias_macro_find(')]
if "char quote = '\\0'" in tokens or "char quote = '\\0'" in arguments:
    raise SystemExit('macro tokenization reintroduced a private quote parser')
print('MINIAS_MACRO_SEPARATOR_OWNER=PASS declaration=1 invocation=1 grammar=shared')
PY
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/nested.s" <<'SRC'
.text
.macro emit_pair v
  .byte \v
  .rept 2
    .byte 0x21
  .endr
.endm
emit_pair 0x10
.rept 2
  .irp v,0x31,0x32
    .byte \v
  .endr
.endr
.macro emit_fields a,b
    .byte \a
    .byte \b
.endm
emit_fields (1 + 2), 4
.byte 0x77
SRC
as --64 -o "$work/gnu.o" "$work/nested.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/nested.s"
for kind in gnu mini; do
    objcopy -j .text -O binary "$work/$kind.o" "$work/$kind.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
expected=$(printf '\020\041\041\061\062\061\062\003\004\167')
cmp "$work/mini.bin" <(printf '%s' "$expected")

for kind in macro rept irp; do
    case "$kind" in
       macro) printf '.macro orphan p\n.byte 0x14\n' >"$work/bad.s"
              expected='unterminated-directive:.macro';;
       rept) printf '.rept 2\n.byte 0x14\n' >"$work/bad.s"
              expected='unterminated-directive:.rept';;
       irp) printf '.irp p,1\n.byte 0x14\n' >"$work/bad.s"
              expected='unterminated-directive:.irp';;
    esac
    if as --64 -o "$work/gnu-bad.o" "$work/bad.s" >/dev/null 2>&1; then
        echo "GNU unexpectedly accepted unterminated $kind" >&2; exit 1
    fi
    rm -f "$work/mini-bad.o"
    if "$MINIAS" --target=x86_64 -o "$work/mini-bad.o" "$work/bad.s" \
          2>"$work/mini.err"; then
        echo "MiniAS accepted unterminated $kind" >&2; exit 1
    fi
    grep -Fq "$expected" "$work/mini.err"
    test ! -e "$work/mini-bad.o"
done
echo 'MINIAS_COLLECTOR_NESTING=PASS GNU_bytes=exact macro_repeat_irp=paired invalid=3'
