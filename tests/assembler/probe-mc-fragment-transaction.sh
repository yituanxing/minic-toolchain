#!/usr/bin/env bash
# Semantic ownership: four target CodeEmitters must not advance Section
# cursors or write partial machine instructions into finalized Sections.
set -Eeuo pipefail
src=assembler/src/assembler.c
grep -q 'target-bypassed-mc-code-emitter' "$src"
python3 - "$src" <<'PY'
import sys
s=open(sys.argv[1],encoding='utf-8').read()
start=s.index('bool minias_emit_instruction_bytes(')
end=s.index('bool minias_section_append(',start)
body=s[start:end]
if 'sec->size +=' in body or 'sec->size++' in body:
    raise SystemExit('machine CodeEmitter changed the final Section cursor')
if 'result->byte_count = needed' not in body:
    raise SystemExit('machine CodeEmitter no longer captures private fragment bytes')
start=s.index('static bool minias_commit_encode_result(')
end=s.index('bool minias_emit_instruction_bytes(',start)
commit=s[start:end]
for invariant in (
    'result->byte_count != owner->size',
    'minias_grow_array((void **)&as->fixups',
    'minias_record_fixup(as,',
    'minias_section_append(as, owner->section,',
    'minias_encode_result_destroy(result)',
):
    if invariant not in commit:
        raise SystemExit('missing complete EncodeResult commit invariant: '+invariant)
if 'minias_section_append(as, owner->section,' not in commit:
    raise SystemExit('missing final Section byte commit')
start=s.index('bool minias_emit_sections(')
end=s.index('int minias_assemble_config(',start)
body=s[start:end]
if 'section->size = (size_t)stmt->offset' in body:
    raise SystemExit('MCFragment still restores a virtual Section cursor')
if 'as->active_encode_result->byte_count != stmt->size' not in body:
    raise SystemExit('missing final encoded fragment size check')
if 'if (!minias_commit_encode_result(as, stmt))' not in body:
    raise SystemExit('MCFragment does not require successful Common commit')
print('MINIAS_MC_FRAGMENT_TRANSACTION=PASS target_cursor=isolated encoded_size=checked commit=atomic')
PY
