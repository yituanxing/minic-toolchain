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
    ('linker_layout.inc', 'linker_script_layout.inc'),
    ('linker_layout.inc', 'linker_vdso_layout.inc'),
    ('linker_layout.inc', 'linker_layout_finalize.inc'),
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
if not (core.index('#include "linker_common_allocation.inc"') <
        core.index('#include "linker_admission.inc"')):
    raise SystemExit('MINILD_LAYOUT_PHASE_STRUCTURE=FAIL common_allocation_order')

# Prevent a second layout state/type owner from silently reappearing.
layout_type = re.compile(r'\btypedef\s+struct\s+MiniLdLayout\s*\{')
for name, contents in sources.items():
    want = 1 if name == 'linker_layout.inc' else 0
    if len(layout_type.findall(contents)) != want:
        raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                         f'layout_type_owner={name}')

owners = {
    'linker_common_allocation.inc': ('minild_allocate_common',),
    'linker_layout.inc': (
        'static_relax_align_section', 'static_build_layout_at',
        'static_build_layout', 'shared_build_layout',
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
        'static_fill_got', 'static_synthesize_runtime_boundaries',
        'minild_layout_destroy', 'static_resolve_symbol',
        'static_entry_address',
    ),
}
for expected_owner, functions in owners.items():
    for function in functions:
        definition = re.compile(
            r'\bstatic\s+(?:bool|void|int|size_t|uint64_t)\s+' +
            re.escape(function) + r'\s*\([^;{}]*\)\s*\{', re.S)
        actual = {name: len(definition.findall(contents))
                  for name, contents in sources.items()}
        if actual.get(expected_owner) != 1 or sum(actual.values()) != 1:
            raise SystemExit(f'MINILD_LAYOUT_PHASE_STRUCTURE=FAIL '
                             f'function={function} definitions={actual}')

print('MINILD_LAYOUT_PHASE_STRUCTURE=PASS modules=5 '
      'single_layout_type=1 unique_phase_functions=15 '
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
echo "MINILD_LAYOUT_PHASE=PASS fixtures=$passed modules=5 compiler=Werror"
