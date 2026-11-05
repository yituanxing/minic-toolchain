#!/usr/bin/env bash
# Architecture-only ET_EXEC/ET_DYN owner contract. Final ELF64 has one
# lifetime for program headers, finalized symbols and section tables, while
# the two output modes keep their existing segment/link policy. Structural
# ownership is checked alongside real ELF byte, symbol, section and PHDR
# oracles; the latter already cover ET_REL -> final and GNU host comparison.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_FINAL_ELF64_OWNER=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-final-elf64-owner.XXXXXX")
finish() {
    if [[ ${MINILD_FINAL_ELF64_OWNER_KEEP:-0} == 1 ]]; then
        echo "MINILD_FINAL_ELF64_OWNER_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

python3 - "$repo/linker/src" <<'PY'
from pathlib import Path
import re
import sys

src = Path(sys.argv[1])

def source(name):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '',
                  (src / name).read_text(), flags=re.S)

def struct_body(text, name):
    match = re.search(r'\btypedef\s+struct\s+' + re.escape(name) +
                      r'\s*\{(.*?)\}\s*' + re.escape(name) + r'\s*;',
                      text, flags=re.S)
    if match is None:
        raise SystemExit(f'final-elf64-owner: missing {name}')
    return match.group(1)

def function_body(text, name):
    match = re.search(r'\b' + re.escape(name) +
                      r'\s*\([^;]*?\)\s*\{', text, flags=re.S)
    if match is None:
        raise SystemExit(f'final-elf64-owner: missing {name}')
    begin, depth = match.end(), 1
    for offset in range(begin, len(text)):
        if text[offset] == '{':
            depth += 1
        elif text[offset] == '}':
            depth -= 1
            if depth == 0:
                return text[begin:offset]
    raise SystemExit(f'final-elf64-owner: unclosed {name}')

core = source('linker_core.inc')
final_owner = source('linker_final_image_plan.inc')
work = struct_body(core, 'MiniLdSection')
if re.search(r'\boutput_name\b', work):
    raise SystemExit('final-elf64-owner: working section retains final output name')

if '#include "linker_final_image_plan.inc"' not in core:
    raise SystemExit('final-elf64-owner: missing final ELF64 owner module')

image = struct_body(final_owner, 'MiniLdFinalImage')
for owner in ('MiniLdProgramPlan', 'MiniLdFinalSymbols',
              'MiniLdFinalSectionTables'):
    if not re.search(r'\b' + owner + r'\b', image):
        raise SystemExit(f'final-elf64-owner: final image lacks {owner}')

# No final semantic owner is allowed to retain an ABI-packed record. The
# output target chooses 32/64 bit packing only after layout and resolution.
for name, descriptor in (
    ('linker_program_plan.inc', 'MiniElfProgramHeader'),
    ('linker_final_symbols.inc', 'MiniElfSymbol'),
    ('linker_final_symbols.inc', 'MiniElfSection'),
    ('linker_output_plan.inc', 'MiniElfSection'),
    ('linker_shared_program_plan.inc', 'MiniElfProgramHeader'),
    ('linker_static_output.inc', 'MiniElfProgramHeader'),
):
    text = source(name)
    if descriptor not in text or re.search(
            r'\bElf64_(?:Phdr|Shdr|Sym)\s*\*', text):
        raise SystemExit(f'final-elf64-owner: ABI packed plan in {name}')

serializer = source('linker_final_elf_image.inc')
for required in ('ELFCLASS32', 'ELFCLASS64',
                 'minild_final_pack_program_header',
                 'minild_final_pack_section_header',
                 'minild_final_pack_symbol',
                 'minild_final_u32_fits'):
    if required not in serializer:
        raise SystemExit(f'final-elf64-owner: serializer lacks {required}')

for name in ('minild_final_image_prepare',
             'minild_final_image_destroy'):
    function_body(final_owner, name)

for forbidden in ('minild_project_admitted_inputs',
                  'process_input_sequence',
                  'work_sections_find_or_add',
                  'shared_prepare_metadata'):
    if forbidden in function_body(final_owner,
                                  'minild_final_image_prepare'):
        raise SystemExit(f'final-elf64-owner: preparation owns input stage {forbidden}')

static = source('linker_static_output.inc')
shared = source('linker_shared_output.inc')
for mode, text, writer, builder, order in (
    ('ET_EXEC', static, 'static_write_executable',
     'minild_static_program_plan_build', 'MINILD_FINAL_SYMTAB_FIRST'),
    ('ET_DYN', shared, 'shared_write_object',
     'minild_shared_program_plan_build', 'MINILD_FINAL_STRTAB_FIRST'),
):
    body = function_body(text, writer)
    for required in (builder, order, 'minild_final_image_prepare',
                     'minild_final_write_image',
                     'minild_final_image_destroy'):
        if required not in body:
            raise SystemExit(f'final-elf64-owner: {mode} lacks {required}')
    if body.index('minild_final_image_prepare') > body.index(
            'minild_final_write_image'):
        raise SystemExit(f'final-elf64-owner: {mode} emits before preparation')
    for forbidden in ('MiniLdFinalSymbols final_symbols',
                      'MiniLdFinalSectionTables final_tables',
                      'minild_final_section_tables_build(',
                      'minild_output_plan_section_headers('):
        if forbidden in body:
            raise SystemExit(f'final-elf64-owner: {mode} duplicates {forbidden}')

emission = function_body(source('linker_final_elf_image.inc'),
                         'minild_final_write_image')
for forbidden in ('minild_final_symbols_build(',
                  'minild_final_section_tables_build(',
                  'minild_output_plan_build(',
                  'shared_prepare_metadata'):
    if forbidden in emission:
        raise SystemExit(f'final-elf64-owner: serializer owns {forbidden}')

print('MINILD_FINAL_ELF64_OWNER_CHECK=PASS stage=architecture '
      'owners=program,symbols,sections modes=ET_EXEC,ET_DYN '
      'work_output_name=removed')
PY

# Existing independent oracles inspect actual ELF64 bytes, final output
# sections, script commands, sh_link, symbols, dynamic tables and segments.
# Invoke scripts directly; never recurse into the enclosing Makefile target.
fixtures=(
    'probe-output-plan-owner-lifecycle:MINILD_OUTPUT_PLAN_OWNER'
    'probe-output-section-owner:MINILD_OUTPUT_SECTION'
    'probe-output-script-edges:MINILD_OUTPUT_EDGES'
    'probe-shared-output-section:MINILD_SHARED_OUTPUT'
    'probe-segment-plan:MINILD_SEGMENTS'
    'probe-final-symbol-view:MINILD_FINAL_SYMBOL'
    'probe-script-nonalloc-offset:MINILD_SHARED_NONALLOC_STRIP'
    'probe-shared-script-dynsym:MINILD_SCRIPT_DYNSYM'
    'probe-relocatable-input-projection:MINILD_RELOCATABLE_PROJECTION'
)
for fixture in "${fixtures[@]}"; do
    script=${fixture%%:*}
    summary=${fixture#*:}
    logfile="$work/$script.log"
    if ! bash "$repo/tests/linker/$script.sh" >"$logfile" 2>&1; then
        echo "MINILD_FINAL_ELF64_OWNER=FAIL fixture=$script" >&2
        tail -n 50 "$logfile" >&2
        exit 1
    fi
    if ! grep -q "^$summary=PASS" "$logfile"; then
        echo "MINILD_FINAL_ELF64_OWNER=FAIL fixture=$script no_evidence=$summary" >&2
        tail -n 30 "$logfile" >&2
        exit 1
    fi
    echo "MINILD_FINAL_ELF64_OWNER_CHECK=PASS fixture=$script"
done

echo "MINILD_FINAL_ELF64_OWNER=PASS architecture=1 fixtures=${#fixtures[@]} ET_REL=separate ET_EXEC=1 ET_DYN=1"
