#!/usr/bin/env bash
# Context is the sole mutable Symbol owner. Source/MC consumers use checked
# SymbolID values; only the terminal ELF writer borrows original const data.
set -Eeuo pipefail

flags=(-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
       -Werror -Iassembler/src -x c -fsyntax-only -)
compiler=${CC:-cc}

reject() {
  local name=$1 source=$2 output
  if output=$(printf '%s\n' "$source" | "$compiler" "${flags[@]}" 2>&1); then
    echo "Context Symbol private-capability breach compiled: $name" >&2
    exit 1
  fi
  if [[ -z $output ]]; then
    echo "Context Symbol negative compile returned no diagnostic: $name" >&2
    exit 1
  fi
}

reject vector_write '
#include "minias_internal.h"
void write_symbol(MiniAs *as) { as->symbols[0].value=42U; }
'
reject name_write '
#include "minias_internal.h"
void rewrite_name(MiniAs *as) { as->symbols[0].name[0]="x"[0]; }
'
reject alias_base_write '
#include "minias_internal.h"
void rewrite_alias(MiniAs *as) { as->symbols[0].alias_section_base[0]="x"[0]; }
'
reject private_owner '
#include "minias_internal.h"
void bypass(MiniAs *as) { as->context_indexes->symbol_records[0].size=11U; }
'
reject mutable_getter '
#include "minias_internal.h"
void borrow_mut(MiniAs *as) {
    MiniAsSymbol *sym=minias_common_context_intern_symbol(as,"candidate");
    (void)sym;
}
'
reject old_split_semantics '
#include "minias_internal.h"
void old_getter(MiniAs *as) { (void)minias_get_symbol(as,"candidate",true); }
'
reject old_const_getter '
#include "minias_common_context.h"
void old_borrow(MiniAs *as) {
    const MiniAsSymbol *symbol=minias_common_context_find_symbol(as,"candidate");
    (void)symbol;
}
'

printf '%s\n' '
#include "minias_common_context.h"
bool valid(MiniAs *as) {
    size_t id;
    MiniAsSymbolView a,b;
    if(!minias_common_context_intern_symbol_id(as,"a",&id) ||
       !minias_common_context_symbol_view(as,id,&a) ||
       a.section!=MINIAS_SECTION_UNDEF)return false;
    return !minias_common_context_find_symbol_view(as,"b",&b) || b.defined;
}
' | "$compiler" "${flags[@]}"

python3 - <<'PY'
from pathlib import Path
import re

root=Path('assembler/src')
private=(root/'common_context.c').read_text()
internal=(root/'minias_internal.h').read_text()
api=(root/'minias_common_context.h').read_text()
assert 'const MiniAsSymbol *symbols;' in internal
assert 'MiniAsSymbol *symbol_records;' in private
assert 'as->symbols=as->context_indexes->symbol_records;' in private
assert 'context_get_symbol_mut(' in private
assert re.search(r'\bstatic MiniAsSymbol \*context_get_symbol_mut\(',private)
assert re.search(r'\bbool minias_common_context_find_symbol_id\(',private)
assert re.search(r'\bbool minias_common_context_intern_symbol_id\(',private)
assert re.search(r'\bbool minias_common_context_layout_symbol_view\(',private)
assert 'MiniAsLayoutSymbolView' in api
for path in root.glob('*.[ch]'):
    text=path.read_text()
    for old in ('minias_get_symbol(', 'minias_common_context_find_symbol(',
                'minias_common_context_intern_symbol(',
                'minias_symbol_ref_lookup('):
        if old in text:
            raise SystemExit('obsolete Symbol borrow: '+str(path)+' '+old)
    if path.name!='common_context.c' and 'symbol_records' in text:
        raise SystemExit('Context private Symbol allocation escaped: '+str(path))
print('MINIAS_CONTEXT_SYMBOL_CONST=PASS negative_compiles=7 '
      'positive_compiles=1 tables=1 entity_getters=removed ids=Context')
PY
