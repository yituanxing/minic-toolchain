#!/usr/bin/env bash
# Real GNU Kbuild input: arm64 vmlinux.lds (Linux 6.6.143) expands
# `__kvm_nvhe_... = ...;;` at top level.  GNU admits empty separators in
# that grammar context (and optional ones inside output-section bodies),
# but not arbitrarily between SECTIONS commands.  Preserve that distinction.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ld nm readelf python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-gnu-empty-commands.XXXXXX")
trap 'if [[ ${MINILD_SCRIPT_EMPTY_KEEP:-0} == 1 ]]; then echo "MINILD_SCRIPT_EMPTY_ARTIFACTS=$work"; else rm -rf -- "$work"; fi' EXIT

cat >"$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.globl kvm_patch_vector_branch
.globl kvm_update_va_mask
_start:
kvm_patch_vector_branch:
    ret
kvm_update_va_mask:
    nop
.section .data,"aw",@progbits
.globl fixture_data
fixture_data:
    .quad 0x1122334455667788
ASM
as -o "$work/entry.o" "$work/entry.s"

cat >"$work/empty.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64);;;
ENTRY(_start);;;
PROVIDE(optional_start = _start);;
__kvm_nvhe_kvm_patch_vector_branch = kvm_patch_vector_branch;;
__kvm_nvhe_kvm_update_va_mask = kvm_update_va_mask;;;
; ;
SECTIONS {
  . = 0x400000;
  .text : { ; *(.text.entry) ;; ; }
  ASSERT(SIZEOF(.text) == 2, "Kernel text size")
  ASSERT(3 > 2 && 3 >= 3 && 2 < 3 && 2 <= 2, "ordered comparisons")
  ASSERT(3 == 3 && 3 != 4 && !(2 > 3), "equality and logical not")
  ASSERT(1 && (0 || 1), "logical precedence")
  . = ALIGN(0x1000);
  .data : { ;; *(.data) ;;; }
}
late_alias = __kvm_nvhe_kvm_patch_vector_branch;;;
LDS

ld -melf_x86_64 -T "$work/empty.lds" -o "$work/gnu.elf" "$work/entry.o"
"$MINILD" -melf_x86_64 -T "$work/empty.lds" \
    -o "$work/mini.elf" "$work/entry.o"
for output in gnu mini; do
    readelf -h "$work/$output.elf" | grep -q 'Machine:.*X86-64'
    nm -n "$work/$output.elf" >"$work/$output.symbols"
    for symbol in _start __kvm_nvhe_kvm_patch_vector_branch \
                  __kvm_nvhe_kvm_update_va_mask fixture_data late_alias; do
        grep -q " $symbol\$" "$work/$output.symbols"
    done
done
python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY_DIFF'
from pathlib import Path
import struct
import sys
def sections(path):
    b = Path(path).read_bytes()
    off = struct.unpack_from('<Q', b, 40)[0]
    num, stridx = struct.unpack_from('<HH', b, 60)
    ss = [struct.unpack_from('<IIQQQQIIQQ', b, off+i*64) for i in range(num)]
    strings = b[ss[stridx][4]:ss[stridx][4]+ss[stridx][5]]
    result = {}
    for s in ss:
        name = strings[s[0]:strings.index(0, s[0])].decode()
        if name in ('.text', '.data'):
            result[name] = (s[3], b[s[4]:s[4]+s[5]])
    return result
assert sections(sys.argv[1]) == sections(sys.argv[2]), 'GNU/MiniLD section differential'
print('MINILD_SCRIPT_EMPTY=PASS GNU_exact_text_data_geometry=1')
PY_DIFF

# A real new directive remains rejected; free semicolons only accept the
# separator token, never arbitrary parenthesized operations or typos.
sed 's/late_alias = __kvm_nvhe_kvm_patch_vector_branch;;;/BROKEN(123);;/' \
    "$work/empty.lds" >"$work/reject.lds"
if "$MINILD" -melf_x86_64 -T "$work/reject.lds" \
    -o "$work/reject.elf" "$work/entry.o" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"; then
    echo 'MINILD_SCRIPT_EMPTY=FAIL unknown_directive_accepted' >&2
    exit 1
fi
grep -q 'unsupported-top-level-linker-script-command' "$work/reject.stderr"
test ! -e "$work/reject.elf"
echo 'MINILD_SCRIPT_EMPTY=PASS GNU_double_semicolon=1 output_body_empty=1 unknown_directive_rejected=1'
