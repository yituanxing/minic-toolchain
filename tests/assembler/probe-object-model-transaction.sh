#!/usr/bin/env bash
# A format-neutral terminal reference plan must sit between Common's
# finalized IDs/bytes and each format writer. There is exactly one resolver
# for Section associations and final Fixup offsets, and one output publisher.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
model_h = (root/'assembler/src/minias_object_model.h').read_text()
model_c = (root/'assembler/src/object_model.c').read_text()
output_c = (root/'assembler/src/object_output.c').read_text()
elf_h = (root/'assembler/src/minias_elf_object_view.h').read_text()
elf_c = (root/'assembler/src/elf_writer.c').read_text()
writer_h = (root/'assembler/src/minias_object_writer.h').read_text()
writer_c = (root/'assembler/src/object_writer.c').read_text()
driver = (root/'assembler/src/assembler.c').read_text()
make = (root/'Makefile').read_text()

for name in ('sections','symbols','fixups'):
    if not re.search(r'const\s+MiniAs'+
                     {'sections':'Section','symbols':'Symbol','fixups':'Fixup'}[name]+
                     r'\s*\*\s*'+name+r'\s*;',model_h):
        raise SystemExit('object model lost const borrow: '+name)
    if 'object.model.'+name+'=' not in driver:
        raise SystemExit('Driver bypasses one terminal model: '+name)
for token in ('typedef struct MiniAsObjectModelView',
              'typedef struct MiniAsObjectSectionPlan',
              'typedef struct MiniAsObjectFixupPlan',
              'minias_object_plan(', 'minias_object_dispose_plan('):
    if token not in model_h or token.startswith('minias_object_') and token not in model_c:
        raise SystemExit('missing logical reference plan: '+token)
for token in ('signature_symbol','linked_section','final_offset',
              'fragment_offset','fragment_location','section_bytes',
              'first_named_section','resolve_link_order'):
    if token not in model_c:
        raise SystemExit('logical reference missing from model: '+token)
for token in ('minielf.h','MiniElf','ELFCLASS','SHN_','SHT_',
              'R_RISCV_', 'R_X86_64_', 'minias_parse_u64(',
              'minias_assemble_file('):
    if token in model_c:
        raise SystemExit('object planner contains format/driver policy: '+token)
for token in ('fragment_location(', 'section_bytes(',
              'resolve_link_order_target(', 'write_elf_atomically(',
              'open(', 'rename(', 'fdopen('):
    if token in elf_c:
        raise SystemExit('ELF serializer still owns common operation: '+token)
if elf_c.count('minias_object_plan(&as->model,')!=1 or \
   elf_c.count('minias_object_dispose_plan(&object_plan)')!=1 or \
   elf_c.count('minias_object_publish_image(')!=1:
    raise SystemExit('object plan/publication not single-use')
if 'MiniAsObjectModelView model;' not in elf_h or \
   'const MiniAsSection *sections;' in elf_h or \
   'const MiniAsFixup *fixups;' in elf_h:
    raise SystemExit('ELF has duplicate direct entity borrows')
for token in ('O_EXCL','rename(', 'fchmod(', 'lstat(',
              'S_ISREG','fwrite(', 'write_stream('):
    if token not in output_c:
        raise SystemExit('atomic output policy lost: '+token)
if 'MiniAsObjectModelView model;' not in writer_h or \
   'const MiniAsObjectSpec *spec;' not in writer_h or \
   'minias_write_object(&object,path)' not in driver or \
   'minias_write_elf_object(' in driver:
    raise SystemExit('Driver bypassed format-neutral object writer dispatch')
if 'MINIAS_OBJECT_FORMAT_ELF' not in writer_c or \
   'unsupported-object-format' not in writer_c or \
   'minias_write_elf_object(&elf,path)' not in writer_c or \
   'project_output_features(' not in writer_c:
    raise SystemExit('format dispatch bypassed ELF policy/feature projection')
for token in ('minias_internal.h','MiniAs *','minias_assemble_file(',
              'SourceGraph','MCLayout','SHT_','SHN_','STB_'):
    if token in writer_h+writer_c:
        raise SystemExit('format dispatcher gained mutable or ELF-wire semantics: '+token)
for path in ('assembler/src/object_model.c',
             'assembler/src/object_writer.c',
             'assembler/src/object_output.c'):
    if path not in make:
        raise SystemExit('new owner not built: '+path)
print('MINIAS_OBJECT_MODEL_TRANSACTION=PASS sections=logical_reference '
      'fixups=final_offset_owned file_output=generic_atomic '
      'ELF=only_format_projection persistent_copy=none')
PY
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Wconversion -Wshadow -Werror \
    -I"$repo/assembler/src" \
    "$repo/assembler/src/object_model.c" \
    "$repo/tests/assembler/object-model-transaction-probe.c" \
    -o "$work/object-model-plan"
"$work/object-model-plan"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wconversion -Wshadow -Werror \
    -I"$repo/assembler/src" \
    "$repo/assembler/src/object_writer.c" \
    "$repo/tests/assembler/object-writer-router-probe.c" \
    -o "$work/object-writer-router"
"$work/object-writer-router"
PYTHONPATH=${PYTHONPATH:-/usr/lib/python3/dist-packages} \
  bash "$repo/tests/assembler/probe-elf-object-view-boundary.sh"
