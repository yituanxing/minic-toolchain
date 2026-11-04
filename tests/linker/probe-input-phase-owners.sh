#!/usr/bin/env bash
# Input-owner boundaries only: preserve a single C translation unit, exact
# admitted source identities, one projection, and the existing link results.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
for tool in python3 ar as ld readelf "${HOST_CC:-cc}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_INPUT_PHASE=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done
for binary in "$MINIAS" "$MINILD"; do
    if [[ ! -x "$binary" ]]; then
        echo "MINILD_INPUT_PHASE=INCONCLUSIVE missing_binary=$binary" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-input-phase.XXXXXX")
finish() {
    if [[ ${MINILD_INPUT_PHASE_KEEP:-0} == 1 ]]; then
        echo "MINILD_INPUT_PHASE_ARTIFACTS=$work"
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
    'linker.c', 'linker_core.inc', 'linker_admission.inc',
    'linker_archive_admission.inc', 'linker_input_sequence.inc',
    'linker_input_projection.inc', 'linker_script_input_selection.inc',
    'linker_input_gc.inc',
)
sources = {}
for name in files:
    path = src / name
    if not path.is_file():
        raise SystemExit(f'MINILD_INPUT_PHASE_STRUCTURE=FAIL missing={name}')
    sources[name] = path.read_text(encoding='utf-8')

makefile = (repo / 'Makefile').read_text(encoding='utf-8')
source_block = re.search(r'^MINILD_SOURCES\s*:=\s*(.*?)^MINILD_OBJECTS\s*:=',
                         makefile, re.M | re.S)
if source_block is None:
    raise SystemExit('MINILD_INPUT_PHASE_STRUCTURE=FAIL build_source_declaration')
build_sources = re.findall(
    r'\b(?:linker/src|tools/minic-ld)/[\w./-]+\.(?:c|inc)\b',
    source_block.group(1),
)
if build_sources.count('linker/src/linker.c') != 1 or any(
        path.endswith('.inc') for path in build_sources):
    raise SystemExit('MINILD_INPUT_PHASE_STRUCTURE=FAIL build_translation_units')

expected_edges = {
    ('linker.c', 'linker_core.inc'),
    ('linker.c', 'linker_input_projection.inc'),
    ('linker.c', 'linker_script_input_selection.inc'),
    ('linker.c', 'linker_input_gc.inc'),
    ('linker_core.inc', 'linker_admission.inc'),
    ('linker_admission.inc', 'linker_archive_admission.inc'),
    ('linker_admission.inc', 'linker_input_sequence.inc'),
}
targets = set(files) - {'linker.c'}
for owner, code in sources.items():
    for target in targets:
        occurrences = re.findall(
            r'^\s*#\s*include\s+"' + re.escape(target) + r'"\s*$',
            code, re.M,
        )
        expected = 1 if (owner, target) in expected_edges else 0
        if len(occurrences) != expected:
            raise SystemExit(
                f'MINILD_INPUT_PHASE_STRUCTURE=FAIL '
                f'include={owner}->{target} actual={len(occurrences)} '
                f'expected={expected}')

driver = sources['linker.c']
admission = sources['linker_admission.inc']
core = sources['linker_core.inc']
if not (driver.index('#include "linker_core.inc"') <
        driver.index('#include "linker_input_projection.inc"') <
        driver.index('#include "linker_script_input_selection.inc"') <
        driver.index('#include "linker_input_gc.inc"') <
        driver.index('static int minild_link_static_pipeline(')):
    raise SystemExit('MINILD_INPUT_PHASE_STRUCTURE=FAIL driver_stage_order')
if not (core.index('#include "linker_common_allocation.inc"') <
        core.index('#include "linker_admission.inc"') <
        core.index('#include "linker_layout.inc"')):
    raise SystemExit('MINILD_INPUT_PHASE_STRUCTURE=FAIL core_stage_order')
if not (admission.index('static bool process_input_data(') <
        admission.index('static bool process_dso_input_data(') <
        admission.index('static bool process_input(') <
        admission.index('#include "linker_archive_admission.inc"') <
        admission.index('#include "linker_input_sequence.inc"')):
    raise SystemExit('MINILD_INPUT_PHASE_STRUCTURE=FAIL admission_stage_order')

functions = {
    'linker_input_projection.inc': (
        'minild_project_admitted_inputs',
        'minild_install_work_section_order',
        'minild_restore_input_order',
        'minild_gc_group_default_output_classes',
        'minild_gc_sort_default_output_alignment',
    ),
    'linker_script_input_selection.inc': (
        'minild_select_script_inputs',
        'minild_shared_prune_script_inputs',
    ),
    'linker_input_gc.inc': (
        'minild_gc_name_has_prefix',
        'minild_gc_root_section',
        'minild_gc_mark_section',
        'minild_gc_prune_static',
    ),
    'linker_admission.inc': (
        'select_input_groups',
        'process_input_data',
        'process_dso_input_data',
        'process_input',
    ),
    'linker_archive_admission.inc': (
        'archive_member_append',
        'read_regular_archive',
        'read_thin_archive_members',
        'archive_extract',
        'archive_open',
    ),
    'linker_input_sequence.inc': (
        'process_whole_archive',
        'process_group_sequence',
        'process_input_sequence',
    ),
}
count = 0
for expected_owner, names in functions.items():
    for name in names:
        definition = re.compile(
            r'\bstatic\s+(?:bool|void|int|size_t|uint64_t|char\s*\*)\s*' +
            re.escape(name) + r'\s*\([^;{}]*\)\s*\{', re.S)
        locations = {owner: len(definition.findall(text))
                     for owner, text in sources.items()}
        if locations.get(expected_owner) != 1 or sum(locations.values()) != 1:
            raise SystemExit(f'MINILD_INPUT_PHASE_STRUCTURE=FAIL '
                             f'function={name} definitions={locations}')
        count += 1

types = (
    ('linker_admission.inc', 'struct', 'MiniLdParsedGroup'),
    ('linker_archive_admission.inc', 'enum', 'MiniLdArchiveKind'),
    ('linker_archive_admission.inc', 'struct', 'MiniLdArchiveMember'),
    ('linker_archive_admission.inc', 'struct', 'MiniLdArchive'),
)
for owner, kind, typename in types:
    definition = re.compile(r'\btypedef\s+' + kind + r'\s+' +
                            typename + r'\s*\{')
    locations = {file: len(definition.findall(text))
                 for file, text in sources.items()}
    if locations.get(owner) != 1 or sum(locations.values()) != 1:
        raise SystemExit(f'MINILD_INPUT_PHASE_STRUCTURE=FAIL '
                         f'type={typename} definitions={locations}')

for func in ('minild_project_admitted_inputs', 'minild_restore_input_order',
             'minild_gc_prune_static', 'minild_select_script_inputs'):
    if func not in driver:
        raise SystemExit(f'MINILD_INPUT_PHASE_STRUCTURE=FAIL '
                         f'driver_missing_call={func}')
shared = (src / 'linker_shared_output.inc').read_text(encoding='utf-8')
if 'minild_project_admitted_inputs(' not in shared:
    raise SystemExit('MINILD_INPUT_PHASE_STRUCTURE=FAIL shared_projection')

print(f'MINILD_INPUT_PHASE_STRUCTURE=PASS modules=6 '
      f'owner_functions={count} owner_types={len(types)} '
      'build=single_translation_unit order=projection,script,gc')
PY

# Existing host behavior oracles: archive fixed point, COMDAT, LINK_ORDER,
# direct and repeated -r projection, script KEEP/DISCARD, GC, ET_DYN and
# relocation byte identities.  No new linker feature is accepted here.
fixtures=(
    'probe-input-graph-only-admission.sh:MINILD_GRAPH_ONLY_ADMISSION'
    'probe-input-projection-single-pass-contract.sh:MINILD_INPUT_PROJECTION'
    'probe-input-work-section-boundary.sh:MINILD_INPUT_WORK'
    'probe-archive-resolution-boundary.sh:MINILD_ARCHIVE_RESOLUTION'
    'probe-relocatable-input-projection.sh:MINILD_RELOCATABLE_PROJECTION'
    'probe-script-input-selection-gc.sh:MINILD_SCRIPT_GC'
    'probe-input-identity-script-order.sh:MINILD_INPUT_ORDER'
    'probe-shared-discard-inputs.sh:MINILD_SHARED_DISCARD'
    'probe-shared-input-identity.sh:MINILD_SHARED_INPUT'
    'probe-shared-synthetic-discard.sh:MINILD_SHARED_SYNTHETIC_DISCARD'
    'probe-default-input-late-coalesce.sh:MINILD_DEFAULT_INPUT'
    'probe-static-pipeline-boundaries.sh:MINILD_STATIC_PIPELINE'
    'probe-as-ld-section-contract.sh:MINILD_AS_SECTION_CONTRACT'
    'probe-symbol-ownership.sh:MINILD_SYMBOL_OWNER'
    'probe-reloc-owner-lifecycle.sh:MINILD_RELOC_OWNER'
)

passed=0
for item in "${fixtures[@]}"; do
    script=${item%%:*}
    marker=${item#*:}
    log="$work/${script%.sh}.log"
    if ! bash "$repo/tests/linker/$script" >"$log" 2>&1 || \
       ! grep -Eq "^${marker}=PASS([[:space:]]|$)" "$log"; then
        echo "MINILD_INPUT_PHASE=FAIL fixture=$script expected=$marker" >&2
        tail -n 60 "$log" >&2
        exit 1
    fi
    echo "MINILD_INPUT_PHASE_CHECK=PASS fixture=$script"
    ((passed += 1))
done
echo "MINILD_INPUT_PHASE=PASS fixtures=$passed modules=6 compiler=Werror"
