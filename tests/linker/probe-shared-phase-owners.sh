#!/usr/bin/env bash
# The ET_DYN owner split is a pure architecture move.  Keep one SharedImage,
# preserve metadata-before-layout / relocation-after-layout ordering, and
# reuse existing real MiniAS/MiniLD fixture contracts for observable bytes.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
for tool in python3 ar as ld readelf "${HOST_CC:-cc}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_SHARED_PHASE=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done
for binary in "$MINIAS" "$MINILD"; do
    if [[ ! -x "$binary" ]]; then
        echo "MINILD_SHARED_PHASE=INCONCLUSIVE missing_binary=$binary" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-phase.XXXXXX")
finish() {
    if [[ ${MINILD_SHARED_PHASE_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_PHASE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

repo = Path(sys.argv[1])
src = repo / 'linker' / 'src'
files = (
    'linker_core.inc',
    'linker_shared_output.inc',
    'linker_shared_pre_layout.inc',
    'linker_shared_finalize.inc',
    'linker_shared_program_plan.inc',
    'linker_shared_reloc_scan.inc',
    'linker_eh_frame.inc',
    'linker_version.inc',
    'linker_shared_reloc_apply.inc',
)
sources = {}
for name in files:
    path = src / name
    if not path.is_file():
        raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL missing={name}')
    sources[name] = path.read_text(encoding='utf-8')

makefile = (repo / 'Makefile').read_text(encoding='utf-8')
build_block = re.search(r'^MINILD_SOURCES\s*:=\s*(.*?)^MINILD_OBJECTS\s*:=',
                        makefile, re.M | re.S)
if build_block is None:
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL build_sources')
compiled = re.findall(
    r'\b(?:linker/src|tools/minic-ld)/[\w./-]+\.(?:c|inc)\b',
    build_block.group(1),
)
if compiled.count('linker/src/linker.c') != 1 or any(
        path.endswith('.inc') for path in compiled):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL build_translation_units')

edges = {
    ('linker_core.inc', 'linker_shared_output.inc'),
    ('linker_shared_output.inc', 'linker_shared_pre_layout.inc'),
    ('linker_shared_output.inc', 'linker_shared_finalize.inc'),
    ('linker_shared_output.inc', 'linker_shared_program_plan.inc'),
    ('linker_shared_pre_layout.inc', 'linker_shared_reloc_scan.inc'),
    ('linker_shared_pre_layout.inc', 'linker_eh_frame.inc'),
    ('linker_shared_finalize.inc', 'linker_version.inc'),
    ('linker_shared_finalize.inc', 'linker_shared_reloc_apply.inc'),
}
targets = set(files) - {'linker_core.inc'}
for name, source in sources.items():
    for target in targets:
        uses = re.findall(
            r'^\s*#\s*include\s+"' + re.escape(target) + r'"\s*$',
            source, re.M,
        )
        want = 1 if (name, target) in edges else 0
        if len(uses) != want:
            raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                             f'include={name}->{target} '
                             f'actual={len(uses)} expected={want}')

driver = sources['linker_shared_output.inc']
pre = sources['linker_shared_pre_layout.inc']
fin = sources['linker_shared_finalize.inc']
prog = sources['linker_shared_program_plan.inc']
if not (driver.index('#include "linker_shared_pre_layout.inc"') <
        driver.index('#include "linker_shared_finalize.inc"') <
        driver.index('#include "linker_shared_program_plan.inc"') <
        driver.index('static bool shared_write_object(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL driver_stage_order')
if not (pre.index('#include "linker_shared_reloc_scan.inc"') <
        pre.index('#include "linker_eh_frame.inc"') <
        pre.index('static bool shared_prepare_metadata(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL reservation_order')
if not (fin.index('static bool shared_finalize_ehdr_symbol(') <
        fin.index('static bool shared_fill_dynsym(') <
        fin.index('#include "linker_version.inc"') <
        fin.index('#include "linker_shared_reloc_apply.inc"') <
        fin.index('static bool shared_fill_dynamic(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL finalization_order')
if not (prog.index('static bool shared_tls_segment(') <
        prog.index('static bool minild_shared_program_plan_build(') <
        prog.index('static void minild_shared_output_section_links(')):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL phdr_policy_order')

layout_type = re.compile(r'\btypedef\s+struct\s+MiniLdSharedImage\s*\{')
owners = {name: len(layout_type.findall(source))
          for name, source in sources.items()}
if owners.get('linker_shared_pre_layout.inc') != 1 or sum(owners.values()) != 1:
    raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL shared_image_owner={owners}')

# Every resolver index has exactly one dynamic-link symbol projection.  The
# old six parallel arrays admitted independently sized/owned symbol maps.
symbol_plan_type = re.compile(r'\btypedef\s+struct\s+MiniLdSharedSymbolPlan\s*\{')
plan_owners = {name: len(symbol_plan_type.findall(source))
               for name, source in sources.items()}
if plan_owners.get('linker_shared_pre_layout.inc') != 1 or \
        sum(plan_owners.values()) != 1 or \
        len(re.findall(r'\bMiniLdSharedSymbolPlan\s*\*symbols\s*;', pre)) != 1:
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL symbol_plan_owner')
symbol_fields = ('dynsym_index', 'dynstr_name_offset', 'plt_index',
                 'got_slot_offset', 'tls_gd_offset', 'preemptible')
plan_body = re.search(r'typedef\s+struct\s+MiniLdSharedSymbolPlan\s*\{(.*?)\}',
                      pre, re.S)
if plan_body is None or any(
        len(re.findall(r'\b' + field + r'\s*;', plan_body.group(1))) != 1
        for field in symbol_fields):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL symbol_plan_fields')
for name, code in sources.items():
    if re.search(r'\bsymbol_map_capacity\b', code) or any(
            re.search(r'\bshared->' + field + r'(?:\[|\b)', code)
            for field in symbol_fields):
        raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'parallel_symbol_maps={name}')
if 'shared->symbols[' not in pre or \
        'shared->symbols[' not in sources['linker_shared_reloc_scan.inc'] or \
        'shared->symbols[' not in sources['linker_shared_reloc_apply.inc'] or \
        'shared->symbols[' not in fin or \
        'shared->symbols[' not in sources['linker_version.inc']:
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL symbol_plan_consumers')

# Only the RISC-V target may choose instruction words.  ET_DYN keeps PLT
# slot allocation and JUMP_SLOT relocations, but delegates AUIPC/LD/JALR/NOP.
target_fixup = (src / 'linker_riscv_fixup.inc').read_text(encoding='utf-8')
shared_apply = sources['linker_shared_reloc_apply.inc']
if len(re.findall(r'\bstatic\s+void\s+minild_riscv_emit_plt_entry\s*\(',
                  target_fixup)) != 1 or \
        shared_apply.count('minild_riscv_emit_plt_entry(') != 1 or \
        re.search(r'\bshared_riscv_(?:u|i)type\b|store_u32le\(plt->data',
                  shared_apply):
    raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL target_plt_owner')
for kind, typename, expected_owner in (
        ('struct', 'MiniLdSharedTlsSegment', 'linker_shared_program_plan.inc'),
        ('enum', 'MiniLdSharedRelocScanResult',
         'linker_shared_reloc_scan.inc')):
    declaration = re.compile(r'\btypedef\s+' + kind + r'\s+' +
                             typename + r'\s*\{')
    actual = {name: len(declaration.findall(code))
              for name, code in sources.items()}
    if actual.get(expected_owner) != 1 or sum(actual.values()) != 1:
        raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'type={typename} owners={actual}')

functions = {
    'linker_shared_pre_layout.inc': (
        'shared_image_destroy', 'shared_elf_hash',
        'shared_gnu_symbol_hash', 'shared_symbol_eligible',
        'shared_symbol_preemptible', 'shared_symbol_needs_plt',
        'shared_define_linker_symbols_pre_layout',
        'shared_prepare_metadata',
    ),
    'linker_shared_finalize.inc': (
        'shared_finalize_ehdr_symbol', 'shared_fill_dynsym',
        'shared_fill_hash', 'shared_fill_gnu_hash', 'shared_fill_dynamic',
    ),
    'linker_shared_program_plan.inc': (
        'shared_tls_segment', 'minild_shared_program_plan_build',
        'minild_shared_output_section_links',
    ),
    'linker_shared_output.inc': (
        'shared_write_object', 'shared_validate_pie_dso_symbols',
        'shared_strip_debug_preflight',
        'shared_script_validate_discarded_alloc',
        'minild_link_shared_elf64_riscv_inputs_options',
        'minild_link_shared_elf64_riscv_inputs',
    ),
    'linker_shared_reloc_apply.inc': (
        'shared_fill_relocations',
        'shared_patch_final_layout_relocations',
        'shared_finalize_plt_and_calls',
    ),
}
definitions = 0
for owner, names in functions.items():
    for func in names:
        pattern = re.compile(
            r'\b(?:static\s+)?(?:bool|void|int|size_t|uint32_t|uint64_t)'
            r'\s+' + re.escape(func) + r'\s*\([^;{}]*\)\s*\{', re.S,
        )
        locations = {module: len(pattern.findall(text))
                     for module, text in sources.items()}
        if locations.get(owner) != 1 or sum(locations.values()) != 1:
            raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                             f'function={func} definitions={locations}')
        definitions += 1

scan_function = re.compile(
    r'\bstatic\s+MiniLdSharedRelocScanResult\s+'
    r'shared_scan_relocation_requirements\s*\([^;{}]*\)\s*\{',
    re.S,
)
scan_owners = {name: len(scan_function.findall(code))
               for name, code in sources.items()}
if scan_owners.get('linker_shared_reloc_scan.inc') != 1 or \
        sum(scan_owners.values()) != 1:
    raise SystemExit(f'MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                     f'function=shared_scan_relocation_requirements '
                     f'definitions={scan_owners}')
definitions += 1

for func in ('shared_prepare_metadata', 'shared_build_vdso_script_layout',
             'shared_fill_dynsym', 'shared_fill_relocations',
             'shared_fill_dynamic', 'shared_write_object'):
    if func + '(' not in driver:
        raise SystemExit('MINILD_SHARED_PHASE_STRUCTURE=FAIL '
                         f'driver_missing_call={func}')

print('MINILD_SHARED_PHASE_STRUCTURE=PASS modules=4 '
      f'owner_functions={definitions} shared_image=single '
      'symbol_plan=single target_plt=1 '
      'build=single_translation_unit order=reserve,fill,program,write')
PY

# Each of these existing integration fixtures exercises actual ELF content,
# PHDRs, dynamic tables, symbols, relocations or failure contracts.  The
# architecture move neither extends ISA coverage nor adds ET_DYN features.
fixtures=(
    'probe-shared-output-section.sh:MINILD_SHARED_OUTPUT'
    'probe-shared-script-dynsym.sh:MINILD_SCRIPT_DYNSYM'
    'probe-shared-input-identity.sh:MINILD_SHARED_INPUT'
    'probe-shared-discard-inputs.sh:MINILD_SHARED_DISCARD'
    'probe-shared-synthetic-discard.sh:MINILD_SHARED_SYNTHETIC_DISCARD'
    'probe-shared-textrel-pie.sh:MINILD_SHARED_TEXTREL_PIE'
    'probe-shared-nonalloc-relocations.sh:MINILD_SHARED_NONALLOC_RELOC'
    'probe-script-nonalloc-offset.sh:MINILD_SHARED_NONALLOC_STRIP'
    'probe-segment-plan.sh:MINILD_SEGMENTS'
    'probe-final-symbol-view.sh:MINILD_FINAL_SYMBOL'
    'probe-output-plan-owner-lifecycle.sh:MINILD_OUTPUT_PLAN_OWNER'
    'probe-final-elf64-owner-lifecycle.sh:MINILD_FINAL_ELF64_OWNER'
    'probe-riscv-final-fixup-parity.sh:MINILD_RISCV_FINAL_FIXUP'
    'probe-reloc-owner-lifecycle.sh:MINILD_RELOC_OWNER'
)

passed=0
for fixture in "${fixtures[@]}"; do
    script=${fixture%%:*}
    marker=${fixture#*:}
    logfile="$work/${script%.sh}.log"
    if ! bash "$repo/tests/linker/$script" >"$logfile" 2>&1 || \
       ! grep -Eq "^${marker}=PASS([[:space:]]|$)" "$logfile"; then
        echo "MINILD_SHARED_PHASE=FAIL fixture=$script expected=$marker" >&2
        tail -n 60 "$logfile" >&2
        exit 1
    fi
    printf 'MINILD_SHARED_PHASE_CHECK=PASS fixture=%s\n' "$script"
    ((passed += 1))
done
echo "MINILD_SHARED_PHASE=PASS fixtures=$passed modules=4 compiler=Werror"
