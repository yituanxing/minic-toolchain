#!/usr/bin/env bash
# Guard the single-translation-unit MiniLD layout owner split.  This is a
# structural and end-to-end regression: no new feature or cross GNU toolchain
# is required, and every fixture exercises the real MiniAS/MiniLD binaries.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
for tool in python3 "${HOST_CC:-cc}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_LAYOUT_PHASE=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done
for binary in "$MINIAS" "$MINILD"; do
    if [[ ! -x "$binary" ]]; then
        echo "MINILD_LAYOUT_PHASE=INCONCLUSIVE missing_binary=$binary" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-layout-phase.XXXXXX")
finish() {
    if [[ ${MINILD_LAYOUT_PHASE_KEEP:-0} == 1 ]]; then
        echo "MINILD_LAYOUT_PHASE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

# Check actual ownership, not merely the names of the extracted files.  The
# linker is intentionally built as one translation unit: each logical layout
# phase must have one definition and one nonrecursive textual include.
python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) / 'linker' / 'src'
names = (
    'linker_layout.inc',
    'linker_script_layout.inc',
    'linker_vdso_layout.inc',
    'linker_layout_finalize.inc',
    'linker_common_allocation.inc',
    'linker_riscv_fixup.inc',
    'linker_align_relax_common.inc',
    'linker_riscv_align_relax.inc',
    'linker_riscv_relax_plan.inc',
    'linker_loongarch_align_relax.inc',
    'linker_static_got.inc',
    'linker_static_reloc_scan.inc',
    'linker_static_reloc_operands.inc',
    'linker_nonriscv_static_fixup.inc',
    'linker_x86_static_fixup.inc',
    'linker_aarch64_static_fixup.inc',
    'linker_loongarch_static_fixup.inc',
    'linker_riscv_static_got.inc',
    'linker_riscv_static_apply.inc',
    'linker_layout_symbols.inc',
    'linker_riscv_default_symbols.inc',
    'linker_static_reloc_apply.inc',
)
sources = {}
for name in names:
    path = root / name
    if not path.is_file():
        raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL missing={name}')
    sources[name] = path.read_text(encoding='utf-8')

core = (root / 'linker_core.inc').read_text(encoding='utf-8')
driver = (root / 'linker.c').read_text(encoding='utf-8')
makefile = (root.parent.parent / 'Makefile').read_text(encoding='utf-8')
sources_block = re.search(r'^MINILD_SOURCES\s*:=\s*(.*?)^MINILD_OBJECTS\s*:=',
                          makefile, re.M | re.S)
if not sources_block:
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL linker_build_sources')
source_paths = re.findall(r'\b(?:linker/src|tools/minic-ld)/[\w./-]+\.(?:c|inc)\b',
                          sources_block.group(1))
if source_paths.count('linker/src/linker.c') != 1 or \
   any(path.endswith('.inc') for path in source_paths):
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL duplicate_layout_build')
includes = {
    'linker_core.inc': core,
    'linker.c': driver,
    **sources,
}

def owner_include(source, target):
    return re.findall(r'^\s*#\s*include\s+"' + re.escape(target) + r'"\s*$',
                      includes[source], re.M)

expected = {
    ('linker.c', 'linker_core.inc'),
    ('linker_core.inc', 'linker_common_allocation.inc'),
    ('linker_core.inc', 'linker_layout.inc'),
    ('linker_layout.inc', 'linker_riscv_fixup.inc'),
    ('linker_layout.inc', 'linker_static_got.inc'),
    ('linker_layout.inc', 'linker_align_relax_common.inc'),
    ('linker_layout.inc', 'linker_riscv_align_relax.inc'),
    ('linker_layout.inc', 'linker_loongarch_align_relax.inc'),
    ('linker_layout.inc', 'linker_script_layout.inc'),
    ('linker_layout.inc', 'linker_vdso_layout.inc'),
    ('linker_layout.inc', 'linker_layout_finalize.inc'),
    ('linker_static_got.inc', 'linker_static_reloc_scan.inc'),
    ('linker_layout_finalize.inc', 'linker_riscv_default_symbols.inc'),
    ('linker_layout_finalize.inc', 'linker_layout_symbols.inc'),
    ('linker_layout_finalize.inc', 'linker_static_reloc_operands.inc'),
    ('linker_layout_finalize.inc', 'linker_nonriscv_static_fixup.inc'),
    ('linker_layout_finalize.inc', 'linker_x86_static_fixup.inc'),
    ('linker_layout_finalize.inc', 'linker_aarch64_static_fixup.inc'),
    ('linker_layout_finalize.inc', 'linker_loongarch_static_fixup.inc'),
    ('linker_layout_finalize.inc', 'linker_riscv_static_got.inc'),
    ('linker_riscv_static_got.inc', 'linker_riscv_relax_plan.inc'),
    ('linker_layout_finalize.inc', 'linker_riscv_static_apply.inc'),
    ('linker_layout_finalize.inc', 'linker_static_reloc_apply.inc'),
}
targets = set(names) | {'linker_core.inc'}
for source, contents in includes.items():
    for target in targets:
        count = len(owner_include(source, target))
        want = 1 if (source, target) in expected else 0
        if count != want:
            raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                             f'include={source}->{target} actual={count} expected={want}')

layout = sources['linker_layout.inc']
if not (layout.index('#include "linker_script_layout.inc"') <
        layout.index('#include "linker_vdso_layout.inc"') <
        layout.index('#include "linker_layout_finalize.inc"')):
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL layout_phase_order')
if not (layout.index('#include "linker_riscv_fixup.inc"') <
        layout.index('#include "linker_static_got.inc"') <
        layout.index('#include "linker_riscv_align_relax.inc"') <
        layout.index('static bool static_build_layout_at(')):
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL prelayout_target_order')
if not (core.index('#include "linker_common_allocation.inc"') <
        core.index('#include "linker_admission.inc"')):
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL common_allocation_order')

# Prevent a second layout state/type owner from silently reappearing.
type_owners = {
    'MiniLdLayout': 'linker_layout.inc',
    'MiniLdPcrelSlot': 'linker_riscv_fixup.inc',
    'MiniLdRiscvRelaxSite': 'linker_riscv_relax_plan.inc',
    'MiniLdRiscvRelaxPlan': 'linker_riscv_relax_plan.inc',
    'MiniLdRelaxEvent': 'linker_align_relax_common.inc',
    'MiniLdStaticTargetPlan': 'linker_static_got.inc',
    'MiniLdStaticRelocOperands': 'linker_static_reloc_operands.inc',
    'MiniLdRiscvStaticGot': 'linker_riscv_static_got.inc',
}
for type_name, expected_owner in type_owners.items():
    pattern = re.compile(r'\btypedef\s+struct\s+' +
                         re.escape(type_name) + r'\s*\{')
    owners_for_type = [name for name, contents in sources.items()
                       for _ in pattern.findall(contents)]
    if owners_for_type != [expected_owner]:
        raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                         f'type={type_name} owners={owners_for_type}')

# Physical placement, script placement and array-boundary synthesis are
# target-neutral.  Any new RISC-V operation must be wired through its explicit
# Target owner, never implemented in a generic layout phase.
for name in ('linker_layout.inc', 'linker_script_layout.inc',
             'linker_layout_finalize.inc', 'linker_static_got.inc',
             'linker_static_reloc_scan.inc',
             'linker_static_reloc_operands.inc',
             'linker_static_reloc_apply.inc'):
    body = re.sub(r'^\s*#\s*include\s+"[^"]+"\s*$', '',
                  sources[name], flags=re.M)
    if re.search(r'\bR_RISCV_\w*|\bstatic_riscv_\w*|\bminild_riscv_\w*',
                 body):
        raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                         f'target_logic_in_generic_layout={name}')

# Non-RISC-V is a family of three independent target codecs, not a second
# architecture switch hidden in a generic static target leaf. The shared
# pass resolves operands and walks sources; only each ISA leaf knows its
# relocation IDs and instruction encodings.
nonrv = sources['linker_nonriscv_static_fixup.inc']
if re.search(r'\b(?:EM_X86_64|EM_AARCH64|EM_LOONGARCH|'
             r'R_X86_64_\w+|R_AARCH64_\w+|R_LARCH_\w+)\b', nonrv) or \
        re.search(r'\bswitch\s*\(\s*machine\s*\)', nonrv):
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                     'machine_switch_in_nonriscv_common')
for leaf, foreign in (
    ('linker_x86_static_fixup.inc', ('R_AARCH64_', 'R_LARCH_')),
    ('linker_aarch64_static_fixup.inc', ('R_X86_64_', 'R_LARCH_')),
    ('linker_loongarch_static_fixup.inc', ('R_X86_64_', 'R_AARCH64_')),
):
    body = sources[leaf]
    if any(token in body for token in foreign):
        raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                         f'foreign_machine_codec={leaf}')

# The generic lifetime is an opaque pointer, and only the RV leaf may know
# GOT slot details. No generic stage may allocate or encode RV GOT bytes.
generic_plan = sources['linker_static_got.inc']
rv_got = sources['linker_riscv_static_got.inc']
if not re.search(r'typedef\s+struct\s+MiniLdStaticTargetPlan\s*\{'
                 r'\s*void\s*\*\s*target_data\s*;\s*\}', generic_plan) or \
        re.search(r'\b(?:MiniLdRiscvStaticGot|got_slot_offset|slot_offsets)\b',
                  generic_plan + sources['linker_static_reloc_scan.inc'] +
                  sources['linker_static_reloc_apply.inc']) or \
        rv_got.count('MiniLdRiscvStaticGot') < 2:
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL opaque_static_plan')

owners = {
    'linker_common_allocation.inc': ('minild_allocate_common',),
    'linker_layout.inc': (
        'static_build_layout_at', 'static_build_layout',
        'shared_build_layout', 'minild_target_relax_section',
    ),
    'linker_riscv_align_relax.inc': (
        'minild_riscv_relax_section',
    ),
    'linker_riscv_relax_plan.inc': (
        'minild_riscv_replan_image_layout',
        'minild_riscv_replan_static_layout',
    ),
    'linker_riscv_fixup.inc': (
        'minild_target_reloc_class',
    ),
    'linker_static_got.inc': (
        'static_target_plan_destroy', 'static_fill_target_relocations',
    ),
    'linker_static_reloc_scan.inc': (
        'static_reserve_target_relocations',
    ),
    'linker_nonriscv_static_fixup.inc': (
        'minild_nonriscv_apply_with_ops',
    ),
    'linker_static_reloc_operands.inc': (
        'minild_static_reloc_source', 'minild_static_reloc_operands',
    ),
    'linker_x86_static_fixup.inc': (
        'minild_x86_apply_static_relocations',
    ),
    'linker_aarch64_static_fixup.inc': (
        'minild_aarch64_apply_static_relocations',
    ),
    'linker_loongarch_static_fixup.inc': (
        'minild_loongarch_apply_static_relocations',
    ),
    'linker_riscv_static_got.inc': (
        'minild_riscv_destroy_static_relocations',
        'minild_riscv_reserve_static_relocations',
        'minild_riscv_fill_static_relocations',
    ),
    'linker_riscv_static_apply.inc': (
        'minild_riscv_apply_static_relocations',
    ),
    'linker_script_layout.inc': (
        'minild_script_declare_shared_symbols', 'script_layout_process_output',
        'static_build_script_layout',
    ),
    'linker_vdso_layout.inc': (
        'shared_validate_vdso_script_phdr_membership',
        'shared_build_vdso_script_layout',
    ),
    'linker_layout_finalize.inc': (
        'static_synthesize_runtime_boundaries',
        'minild_layout_destroy',
    ),
    'linker_riscv_default_symbols.inc': (
        'minild_riscv_synthesize_gnu_default_boundaries',
    ),
    'linker_layout_symbols.inc': (
        'minild_layout_resolve_symbol', 'static_entry_address',
    ),
    'linker_static_reloc_apply.inc': (
        'static_apply_relocations',
    ),
}
for expected_owner, functions in owners.items():
    for function in functions:
        definition = re.compile(
            r'\bstatic\s+(?:bool|void|int|size_t|uint64_t|MiniLdTargetRelocClass)\s+' +
            re.escape(function) + r'\s*\([^;{}]*\)\s*\{', re.S)
        actual = {name: len(definition.findall(contents))
                  for name, contents in sources.items()}
        if actual.get(expected_owner) != 1 or sum(actual.values()) != 1:
            raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                             f'function={function} definitions={actual}')

print('MINILD_LAYOUT_PHASE_STRUCTURE=PASS modules=21 '
      'single_layout_type=1 target_opaque_static_plan=1 '
      'explicit_target_owners=10 unique_phase_functions=30 '
      'include_graph=nonrecursive build=single_translation_unit')
PY

# Every fixture is a preexisting host integration contract.  In particular,
# script BYTE/AT, R_RISCV_ALIGN, GC, ET_REL -r, static high-half GOT, ET_DYN
# vDSO PHDR membership and final program headers all remain observable.
fixtures=(
    'probe-output-plan-owner-lifecycle.sh:MINILD_OUTPUT_PLAN_OWNER'
    'probe-output-script-edges.sh:MINILD_OUTPUT_EDGES'
    'probe-script-input-selection-gc.sh:MINILD_SCRIPT_GC'
    'probe-symbol-ownership.sh:MINILD_SYMBOL_OWNER'
    'probe-reloc-owner-lifecycle.sh:MINILD_RELOC_OWNER'
    'probe-riscv-final-fixup-parity.sh:MINILD_RISCV_FINAL_FIXUP'
    'probe-segment-plan.sh:MINILD_SEGMENTS'
    'probe-shared-output-section.sh:MINILD_SHARED_OUTPUT'
    'probe-shared-script-dynsym.sh:MINILD_SCRIPT_DYNSYM'
    'probe-script-nonalloc-offset.sh:MINILD_SHARED_NONALLOC_STRIP'
    'probe-final-symbol-view.sh:MINILD_FINAL_SYMBOL'
    'probe-relocatable-input-projection.sh:MINILD_RELOCATABLE_PROJECTION'
    'probe-static-pipeline-boundaries.sh:MINILD_STATIC_PIPELINE'
    'probe-output-section-owner.sh:MINILD_OUTPUT_SECTION'
    'probe-input-identity-script-order.sh:MINILD_INPUT_ORDER'
    'probe-shared-synthetic-discard.sh:MINILD_SHARED_SYNTHETIC_DISCARD'
)

passed=0
for fixture in "${fixtures[@]}"; do
    script=${fixture%%:*}
    marker=${fixture#*:}
    log="$work/${script%.sh}.log"
    if ! bash "$root/tests/linker/$script" >"$log" 2>&1 || \
       ! grep -Eq "^${marker}=PASS([[:space:]]|$)" "$log"; then
        echo "MINILD_LAYOUT_PHASE=FAIL fixture=$script expected=$marker" >&2
        tail -n 60 "$log" >&2
        exit 1
    fi
    printf 'MINILD_LAYOUT_PHASE_CHECK=PASS fixture=%s\n' "$script"
    ((passed += 1))
done

parser_log="$work/script-a0.log"
if ! HOST_CC="${HOST_CC:-cc}" BUILD_DIR="$work/parser" \
     sh "$root/tests/linker/run-script-a0.sh" >"$parser_log" 2>&1 || \
   ! grep -Eq '^MINILD_SCRIPT_A0=PASS([[:space:]]|$)' "$parser_log"; then
    echo 'MINILD_LAYOUT_PHASE=FAIL fixture=run-script-a0.sh' >&2
    tail -n 60 "$parser_log" >&2
    exit 1
fi
echo 'MINILD_LAYOUT_PHASE_CHECK=PASS fixture=run-script-a0.sh'
((passed += 1))
echo "MINILD_LAYOUT_PHASE=PASS fixtures=$passed modules=19 compiler=Werror"
