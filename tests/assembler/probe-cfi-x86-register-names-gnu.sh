#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as readelf cmp python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# GNU i386 CFI accepts both bare GAS register identifiers (used by Linux
# arch/x86/entry/vdso/vdso32/system_call.S) and AT&T percent-prefixed forms.
# DWARF CFI register numbers are ABI-specific, not x86 machine ModRM IDs.
cat >"$work/i386.s" <<'ASM'
.cfi_sections .debug_frame
.text
.globl cfi_i386_names
cfi_i386_names:
.cfi_startproc
.cfi_def_cfa esp, 4
.cfi_offset %ebx, -8
nop
.cfi_rel_offset ecx, 0
.cfi_register eax, edx
.cfi_undefined %esi
.cfi_same_value edi
.cfi_restore %ebx
.cfi_def_cfa_register %ebp
.cfi_return_column eip
nop
.cfi_endproc
ASM

cat >"$work/amd64.s" <<'ASM'
.cfi_sections .debug_frame
.text
.globl cfi_amd64_names
cfi_amd64_names:
.cfi_startproc
.cfi_def_cfa %rsp, 8
.cfi_offset rbp, -16
nop
.cfi_rel_offset rcx, 0
.cfi_register %rax, %rdx
.cfi_undefined rsi
.cfi_same_value %rdi
.cfi_restore rbp
.cfi_def_cfa_register rsp
.cfi_return_column rip
nop
.cfi_endproc
ASM

for bits in 32 64; do
  if [[ "$bits" == 32 ]]; then
    source=i386
    mini_args=(--target=x86_64 -march=i386 -mabi=i386)
  else
    source=amd64
    mini_args=(--target=x86_64)
  fi
  as "--$bits" -o "$work/gnu.o" "$work/$source.s"
  "$MINIAS" "${mini_args[@]}" -o "$work/mini.o" "$work/$source.s"
  readelf -x .debug_frame "$work/gnu.o" >"$work/gnu.bytes"
  readelf -x .debug_frame "$work/mini.o" >"$work/mini.bytes"
  cmp "$work/gnu.bytes" "$work/mini.bytes"

  readelf --debug-dump=frames "$work/gnu.o" >"$work/gnu.frames"
  readelf --debug-dump=frames "$work/mini.o" >"$work/mini.frames"
  python3 - "$bits" "$work/gnu.frames" "$work/mini.frames" <<'PY'
import pathlib
import re
import sys

bits, gnu_path, mini_path = sys.argv[1:]

def events(path):
    content = pathlib.Path(path).read_text()
    rules = [line.split('DW_CFA_', 1)[1].strip()
             for line in content.splitlines() if 'DW_CFA_' in line]
    return [event for event in rules
            if event != 'nop' and not event.startswith('advance_loc')]

gnu, mini = events(gnu_path), events(mini_path)
if gnu != mini:
    raise SystemExit(f'ELF{bits} named-register mismatch:\nGNU={gnu}\nMini={mini}')
assert any('register:' in event for event in gnu), gnu
assert any('offset:' in event for event in gnu), gnu
print(f'MINIAS_CFI_REGISTER_NAMES=PASS ELF{bits} GNU_frame_bytes=equal rules=exact')
PY
done

for reg in 'not_a_register' '%not_a_register' 'ecxfoobar' 'r1234'; do
  cat >"$work/reject.s" <<ASM
.text
f:
.cfi_startproc
.cfi_rel_offset $reg,0
nop
.cfi_endproc
ASM
  for bits in 32 64; do
    if as "--$bits" -o "$work/gnu.o" "$work/reject.s" >/dev/null 2>&1; then
      echo "GNU accepted invalid CFI register $reg ELF$bits" >&2
      exit 1
    fi
    if [[ "$bits" == 32 ]]; then
      mini_args=(--target=x86_64 -march=i386 -mabi=i386)
    else
      mini_args=(--target=x86_64)
    fi
    if "$MINIAS" "${mini_args[@]}" -o "$work/mini.o" "$work/reject.s" >/dev/null 2>&1; then
      echo "MiniAS accepted invalid CFI register $reg ELF$bits" >&2
      exit 1
    fi
  done
done
echo 'MINIAS_CFI_REGISTER_NAMES_REJECT=PASS invalid=8 GNU_exact'
