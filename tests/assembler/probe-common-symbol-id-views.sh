#!/usr/bin/env bash
# CFI, DWARF, Emission and Driver query stable SymbolIDs through Context.
set -Eeuo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source="$repo/tests/assembler/common-symbol-id-views-compile.c"
compiler=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

flags=(-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
       -fsyntax-only -I"$repo/assembler/src")
"$compiler" "${flags[@]}" "$source"
for case in ALIAS_EDGE ALIAS_BASE FRAGMENT_ANCHOR SIZE_PLAN NAME_WRITE ENTITY_BORROW; do
    if "$compiler" "${flags[@]}" -D"TRY_$case" "$source" \
        >"$work/$case.log" 2>&1; then
        echo "MINIAS_SYMBOL_ID_VIEWS=FAIL compile_case=$case" >&2
        exit 1
    fi
done

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

root=Path(sys.argv[1])/'assembler/src'
context=(root/'common_context.c').read_text()
api=(root/'minias_common_context.h').read_text()
driver=(root/'assembler.c').read_text()
emission=(root/'common_emission.c').read_text()
layout=(root/'mc_layout.c').read_text()

for method in ('symbol_view', 'find_symbol_view', 'symbol_ref_view',
               'intern_symbol_id'):
    name='minias_common_context_'+method
    if not re.search(r'\bbool\s+'+name+r'\s*\(',context) or name not in api:
        raise SystemExit('missing Context-owned SymbolID service: '+name)
if ('minias_project_symbol(&as->symbols[id],out)' not in context or
    'symbol-as->context_indexes->symbol_records' not in context or
    'minias_project_symbol(minias_symbol_ref_lookup(as,ref),out)' not in context):
    raise SystemExit('Context SymbolID/value conversion was duplicated')
for filename in ('cfi_writer.c','debug_line.c','common_emission.c'):
    code=(root/filename).read_text()
    for forbidden in (r'\bas\s*->\s*symbols\b',
                      r'\bas\s*->\s*symbol_count\b',
                      r'\bMiniAsSymbol\s*\*',
                      r'\bminias_symbol_ref_lookup\s*\('):
        if re.search(forbidden,code):
            raise SystemExit(filename+' bypassed SymbolID view: '+forbidden)
    if 'minias_common_context_symbol_view(' not in code:
        raise SystemExit(filename+' lacks checked SymbolID reads')
if 'minias_common_context_intern_symbol_id(' not in emission or \
   'minias_common_context_find_symbol_view(' not in emission or \
   re.search(r'\bsymbol\s*-\s*as\s*->\s*symbols',emission):
    raise SystemExit('Emission still retains a Symbol entity or derives ID')
for name in ('minias_parse_symbol_pair_same_defined_section',
             'minias_common_source_symbol_defined'):
    body=driver.split('bool '+name+'(',1)[1].split('\n}',1)[0]
    if 'minias_common_context_' not in body or \
       re.search(r'\bMiniAsSymbol\s*\*',body):
        raise SystemExit('Driver source Symbol consumer bypassed Context: '+name)
for name,code in (('common_emission.c',emission),('mc_layout.c',layout)):
    if 'return minias_common_context_symbol_ref_view(ctx->as,ref,out);' not in code:
        raise SystemExit(name+' duplicates Context Target projection')
if 'object.symbols=as->symbols' not in driver:
    raise SystemExit('ELF lost its separate terminal const Symbol borrow')
print('MINIAS_SYMBOL_ID_VIEWS=PASS consumers=CFI,DWARF,Emission,Driver '
      'identity=Context target_queries=Context elf_borrow=unchanged')
PY

echo 'MINIAS_SYMBOL_ID_VIEWS_COMPILE=PASS positive=1 negative=6'
