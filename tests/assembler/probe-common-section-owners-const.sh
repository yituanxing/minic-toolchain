#!/usr/bin/env bash
# A SectionID joins two independent ownership domains: immutable metadata
# in Context and emitted byte payload in Emission. No shared mutable Section.
set -Eeuo pipefail

compiler=${CC:-cc}
flags=(-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
       -Werror -Iassembler/src -x c -fsyntax-only -)
reject() {
  local name=$1 source=$2 output
  if output=$(printf '%s\n' "$source" | "$compiler" "${flags[@]}" 2>&1); then
    echo "Section ownership bypass compiled: $name" >&2
    exit 1
  fi
  if [[ -z $output ]]; then
    echo "Section negative compile produced no diagnostic: $name" >&2
    exit 1
  fi
}

reject metadata_write '
#include "minias_internal.h"
void write_metadata(MiniAs *as) { as->sections[0].align=8U; }
'
reject metadata_name_write '
#include "minias_internal.h"
void rewrite_name(MiniAs *as) { as->sections[0].name[0]="x"[0]; }
'
reject old_byte_vector '
#include "minias_internal.h"
void write_old_bytes(MiniAs *as) { as->sections[0].data[0]=1U; }
'
reject old_output_length '
#include "minias_internal.h"
void change_length(MiniAs *as) { as->sections[0].size=17U; }
'
reject mutable_borrow '
#include "minias_internal.h"
void borrow(MiniAs *as) { MiniAsSection *s=&as->sections[0]; (void)s; }
'
reject private_context '
#include "minias_internal.h"
void context_leak(MiniAs *as) { as->context_indexes->section_records[0].type=1U; }
'
reject private_emission '
#include "minias_internal.h"
void emission_leak(MiniAs *as) { as->emission->section_bytes[0].size=0U; }
'
reject emitted_byte_write '
#include "minias_common_emission.h"
void alter(MiniAs *as) {
    MiniAsEmittedSectionView view;
    if(minias_emission_section_view(as,0,&view))view.data[0]=0U;
}
'

printf '%s\n' '
#include "minias_common_emission.h"
bool observe(const MiniAs *as) {
    const MiniAsSection *metadata=&as->sections[0];
    MiniAsEmittedSectionView output;
    return metadata->align!=0U &&
           minias_emission_section_view(as,0,&output) &&
           output.size==minias_emission_section_size(as,0);
}
' | "$compiler" "${flags[@]}"

python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
internal=(root/'minias_internal.h').read_text()
records=(root/'minias_object_records.h').read_text()
context=(root/'common_context.c').read_text()
private=(root/'minias_common_emission_private.h').read_text()
emission=(root/'common_emission.c').read_text()
layout=(root/'mc_layout.c').read_text()
meta=records.split('typedef struct MiniAsSection {',1)[1].split('} MiniAsSection;',1)[0]
if re.search(r'\b(?:data|size|capacity)\s*;',meta):
    raise SystemExit('Section metadata still contains byte-owner fields')
if 'const MiniAsSection *sections;' not in internal or \
   'MiniAsSection *section_records;' not in context or \
   'as->sections=as->context_indexes->section_records;' not in context:
    raise SystemExit('Context Section metadata allocation not private')
if 'MiniAsEmittedSection *section_bytes;' not in private or \
   'emission_mutable_section(' not in emission:
    raise SystemExit('Emission output byte allocation not private')
if 'minias_emission_clear_leb_payload(' not in layout or \
   re.search(r'\bsection->data\s*\[',layout):
    raise SystemExit('Layout still patches Section bytes directly')
print('MINIAS_SECTION_CONST_OWNERS=PASS negative_compiles=8 '
      'positive_compiles=1 metadata=Context bytes=Emission LEB_patch=Emission')
PY
