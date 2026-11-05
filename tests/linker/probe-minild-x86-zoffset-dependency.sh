#!/usr/bin/env bash
# A synthetic Kbuild derivative audit: old GNU zoffset and header.S are
# immutable controls, while new compressed ELF offsets require reassembling
# exactly header.S. No C compilation, full kernel or ABI shortcut is involved.
set -Eeuo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
helper="$repo/tools/ci/minild-x86-linux-frozen-full-bzimage-v1.sh"
audit="$repo/tools/ci/minild-x86-zoffset-header-audit.py"
for command in gcc nm readelf python3; do
    command -v "$command" >/dev/null || {
        echo "MINILD_X86_ZOFFSET_DEP=FAIL missing_tool=$command" >&2
        exit 1
    }
done
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
mkdir -p "$work/old" "$work/mini" "$work/source/arch/x86/boot"

for owner in old mini; do
    if [[ $owner == old ]]; then
        compressed=0xc35469
        entry=0xc3e959
        text=0xc35730
    else
        compressed=0xc35510
        entry=0xc3ea09
        text=0xc357e0
    fi
    printf '%s\n' \
        '.globl startup_32, efi32_stub_entry, efi64_stub_entry' \
        '.globl efi32_pe_entry, efi_pe_entry, input_data, kernel_info' \
        '.globl _end, _ehead, _text, _data, _edata, z_input_len, z_output_len' \
        '.set startup_32, 0' \
        '.set efi32_stub_entry, 0xc3c8c0' \
        '.set efi64_stub_entry, 0xc3cac0' \
        '.set efi32_pe_entry, 0xc3c9d0' \
        ".set efi_pe_entry, $entry" \
        '.set input_data, 0x2c4' \
        '.set kernel_info, 0xc444a8' \
        '.set _end, 0xc81000' \
        '.set _ehead, 0x2c4' \
        ".set _text, $text" \
        '.set _data, 0xc46000' \
        '.set _edata, 0xc46400' \
        ".set z_input_len, $compressed" \
        '.set z_output_len, 0x2947330' >"$work/$owner/target.s"
    gcc -m32 -x assembler -c "$work/$owner/target.s" \
        -o "$work/$owner/compressed.o"
    bash "$helper" make-zoffset "$work/$owner/compressed.o" \
        "$work/$owner/zoffset.h" >"$work/$owner/zoffset.log"
    grep -Eq "^#define ZO_z_input_len 0x0*$(printf %x "$compressed")$" \
        "$work/$owner/zoffset.h"
done

printf '%s\n' \
    '#include "zoffset.h"' \
    '.section .bstext,"ax",@progbits' \
    '.globl optional_header' \
    'optional_header:' \
    '    .space 16' \
    '    .long setup_size + ZO_efi_pe_entry' \
    '.section .header,"a",@progbits' \
    '.globl payload_length' \
    'payload_length:' \
    '    .long ZO_z_input_len' \
    '    .long ZO__data' \
    '.section .entrytext,"ax",@progbits' \
    '    .long ZO_efi32_stub_entry' \
    '.section .initdata,"a",@progbits' \
    '    .long ZO_kernel_info' >"$work/source/arch/x86/boot/header.S"
for owner in old mini; do
    gcc -m32 -x assembler-with-cpp -c \
        -I"$work/$owner" "$work/source/arch/x86/boot/header.S" \
        -o "$work/$owner/header.o"
done
cp "$work/old/header.o" "$work/original-GNU-header.o"
python3 "$audit" "$work/original-GNU-header.o" "$work/old/header.o" \
    "$work/mini/header.o" "$work/old/zoffset.h" "$work/mini/zoffset.h" \
    >"$work/audit-pass.log"
grep -Fq 'MINILD_X86_HEADER_AUDIT=PASS' "$work/audit-pass.log"

# The source remap is constrained to the captured GNU header.S, and rejects
# an unexpected C source or flags rather than compiling it.
printf '%s\n' \
    'savedcmd_arch/x86/boot/header.o := gcc -m16 -D_SETUP -D__ASSEMBLY__ -c -o arch/x86/boot/header.o /producer/linux/arch/x86/boot/header.S' \
    >"$work/original-GNU-header.o.cmd"
bash "$helper" check-header-as-argv "$work/original-GNU-header.o.cmd" \
    "$work/new-header.o" "$work/source" "$work/new-header.argv0" \
    >"$work/as-argv-pass.log"
python3 - "$work/new-header.argv0" "$work/source" "$work/new-header.o" <<'PY'
from pathlib import Path
import sys
args = Path(sys.argv[1]).read_bytes().rstrip(b'\0').decode().split('\0')
if not args or not args[0].startswith('-') or args.count('-c') != 1:\n    raise SystemExit('MINILD_X86_ZOFFSET_DEP=FAIL executable_leaked_or_compile_flag')\nif args[-1] != sys.argv[2] + '/arch/x86/boot/header.S':
    raise SystemExit('MINILD_X86_ZOFFSET_DEP=FAIL wrong_source_remap')
if args[args.index('-o') + 1] != sys.argv[3] or '-m16' not in args:
    raise SystemExit('MINILD_X86_ZOFFSET_DEP=FAIL wrong_asm_command')
PY

printf '%s\n' \
    'savedcmd_arch/x86/boot/header.o := gcc -m16 -D_SETUP -D__ASSEMBLY__ -c -o arch/x86/boot/header.o /producer/linux/arch/x86/boot/header.c' \
    >"$work/bad-header.o.cmd"
if bash "$helper" check-header-as-argv "$work/bad-header.o.cmd" \
    "$work/new-header.o" "$work/source" "$work/bad-header.argv0" \
    >"$work/as-argv-fail.log" 2>&1; then
    echo 'MINILD_X86_ZOFFSET_DEP=FAIL C_source_was_admitted' >&2
    exit 1
fi
grep -q 'unsupported_GNU_header_assembly_command' "$work/as-argv-fail.log"

# A corrupted new payload length is not explainable by zoffset and must fail.
printf '%s\n' \
    '#include "zoffset.h"' \
    '.section .header,"a",@progbits' \
    '.globl payload_length' \
    'payload_length:' \
    '    .long 0xdeadbeef' >"$work/source/arch/x86/boot/forged-header.S"
gcc -m32 -x assembler-with-cpp -c -I"$work/mini" \
    "$work/source/arch/x86/boot/forged-header.S" \
    -o "$work/forged-header.o"
if python3 "$audit" "$work/original-GNU-header.o" "$work/old/header.o" \
    "$work/forged-header.o" "$work/old/zoffset.h" "$work/mini/zoffset.h" \
    >"$work/forged-header.log" 2>&1; then
    echo 'MINILD_X86_ZOFFSET_DEP=FAIL forged_header_admitted' >&2
    exit 1
fi
grep -Eq 'header-relocation-records-changed|header-allocated-section-geometry-changed|header-placed-symbol-identities-or-offsets-changed|MiniLD-header-payload-length-inconsistent' \
    "$work/forged-header.log" || {
    echo 'MINILD_X86_ZOFFSET_DEP=FAIL wrong_forged_rejection_reason' >&2
    tail -n 20 "$work/forged-header.log" >&2
    exit 1
}

# An unrelated word change IN an allowed boot section must still fail. A
# section-level allowance would be too broad for a trusted frozen C image.
sed 's/    .long ZO_kernel_info/    .long 0xdeadbeef/' \
    "$work/source/arch/x86/boot/header.S" \
    >"$work/source/arch/x86/boot/forged-byte.S"
gcc -m32 -x assembler-with-cpp -c -I"$work/mini" \
    "$work/source/arch/x86/boot/forged-byte.S" \
    -o "$work/forged-byte.o"
if python3 "$audit" "$work/original-GNU-header.o" "$work/old/header.o" \
    "$work/forged-byte.o" "$work/old/zoffset.h" "$work/mini/zoffset.h" \
    >"$work/forged-byte.log" 2>&1; then
    echo 'MINILD_X86_ZOFFSET_DEP=FAIL unrelated_header_byte_admitted' >&2
    exit 1
fi
grep -q 'unexpected-header-byte-change:.initdata' "$work/forged-byte.log"

echo 'MINILD_X86_ZOFFSET_DEP=PASS original_nm_sed=1 GNU_AS_only=1 derived_payload=1 EFI_delta=1 replay_parity=1 strict_header_layout=1 forbidden_byte_rejected=1 forged_rejected=1 C_forbidden=1'
