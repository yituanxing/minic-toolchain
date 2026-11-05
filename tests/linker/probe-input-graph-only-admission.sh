#!/usr/bin/env bash
# Admission is a stable InputGraph transaction.  No provisional WorkSection,
# placement or second ELF-section index map may be populated while admitting
# ELF objects (including archive members).  Post-admission projection alone
# creates work sections.  The behavior oracle reuses narrow, independent
# ELF/output fixtures rather than relying on diagnostic phase spellings.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"

for tool in python3 ar readelf as ld; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_GRAPH_ONLY_ADMISSION=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-graph-admission.XXXXXX")
cleanup() {
    if [[ ${MINILD_GRAPH_ONLY_KEEP:-0} == 1 ]]; then
        echo "MINILD_GRAPH_ONLY_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

# This is deliberately a source-architecture contract, not a test of the
# names used in profile diagnostics.  Inspect actual admission function
# bodies; a WorkSection API is still legitimate in the later projector.
python3 - "$repo/linker/src/linker_core.inc" \
    "$repo/linker/src/linker.c" \
    "$repo/linker/src/linker_shared_output.inc" <<'PY'
from pathlib import Path
import re
import sys

core_path = Path(sys.argv[1])
core = core_path.read_text()
static, shared = [Path(p).read_text() for p in sys.argv[2:]]
admission_include = 'linker_admission.inc'
if (core_path.with_name(admission_include)).is_file():
    if not re.search(r'#include\s+"' + admission_include + r'"', core):
        raise SystemExit('graph-only: physical admission owner exists but '
                         'Core does not include it')
    admission = core_path.with_name(admission_include).read_text()
else:
    # Earlier revisions owned these functions physically in Core.
    admission = core


def body_of(source, function, required=True):
    # Strip comments before extracting balanced function bodies; quoted
    # diagnostics contain no control-flow ownership semantics.
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    match = re.search(r'\b' + re.escape(function) +
                      r'\s*\([^;]*?\)\s*\{', source, flags=re.S)
    if match is None:
        if required:
            raise SystemExit(f'graph-only: missing admission function {function}')
        return ''
    start = match.end()
    nesting = 1
    for pos in range(start, len(source)):
        if source[pos] == '{':
            nesting += 1
        elif source[pos] == '}':
            nesting -= 1
            if nesting == 0:
                return source[start:pos]
    raise SystemExit(f'graph-only: unbalanced function {function}')


for name in ('process_input_data', 'admit_input_section'):
    body = body_of(admission, name, required=name == 'process_input_data')
    for forbidden in ('work_sections_find_or_add',
                      'find_or_add_section_with_identity',
                      'work_placement_append', 'state->work.'):
        if forbidden in body:
            raise SystemExit(f'graph-only: {name} still writes work state '
                             f'via {forbidden}')

if re.search(r'\bMiniLdSectionMap\b', core + admission):
    raise SystemExit('graph-only: duplicate transient MiniLdSectionMap remains')

for required in ('input_graph_record_file', 'input_graph_record_section',
                 'input_graph_record_symbol', 'select_input_groups'):
    if required not in admission:
        raise SystemExit(f'graph-only: missing stable source owner {required}')

for name, source in (('static ET_EXEC', static),
                     ('shared ET_DYN', shared),
                     ('partial ET_REL', core)):
    if 'minild_project_admitted_inputs(' not in source:
        raise SystemExit(f'graph-only: {name} bypasses post-admission projector')

print('MINILD_GRAPH_ONLY_CHECK=PASS stage=architecture '
      'admission=InputGraph projection=postadmission maps=single')
PY

# Each fixture is independently maintained and checks output ELF sections,
# symbols and relocation bytes. Their union covers regular/thin archives,
# lazy/group rescans, COMDAT first-wins signature/members, LINK_ORDER,
# repeated -r, GC, file-qualified scripts and ET_DYN dynamic metadata.
for fixture in \
    probe-archive-resolution-boundary \
    probe-as-ld-section-contract \
    probe-relocatable-input-projection \
    probe-input-work-section-boundary \
    probe-input-projection-single-pass-contract \
    probe-shared-input-identity; do
    name=${fixture#probe-}
    logfile="$work/$name.log"
    if ! bash "$repo/tests/linker/$fixture.sh" >"$logfile" 2>&1; then
        echo "MINILD_GRAPH_ONLY_ADMISSION=FAIL fixture=$fixture" >&2
        tail -n 45 "$logfile" >&2
        exit 1
    fi
    if ! grep -Eq '^[A-Z_]+=PASS ' "$logfile"; then
        echo "MINILD_GRAPH_ONLY_ADMISSION=FAIL fixture=$fixture no_evidence" >&2
        tail -n 25 "$logfile" >&2
        exit 1
    fi
    echo "MINILD_GRAPH_ONLY_CHECK=PASS fixture=$name"
done

echo 'MINILD_GRAPH_ONLY_ADMISSION=PASS architecture=1 fixtures=6 '
echo '  archive=regular+thin COMDAT=signature+members LINK_ORDER=1 -r=twice GC=1 script=1 ET_DYN=1'
