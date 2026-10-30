#!/usr/bin/env bash
# Semantic ownership: four target CodeEmitters must not advance Section
# cursors or write partial machine instructions into finalized Sections.
set -Eeuo pipefail
src=assembler/src/assembler.c
# The old capture_instruction_bytes switch was the two-commit-path bridge.
! grep -q 'capture_instruction_bytes' "$src"
grep -q 'minias_stage_fragment_bytes' "$src"
grep -q 'minias_stage_fragment_pattern' "$src"
python3 - "$src" <<'PY'
import sys
s=open(sys.argv[1],encoding='utf-8').read()
start=s.index('bool minias_emit_instruction_bytes(')
end=s.index('bool minias_section_append(',start)
body=s[start:end]
if 'sec->size +=' in body or 'sec->size++' in body:
    raise SystemExit('machine CodeEmitter changed the final Section cursor')
if 'minias_stage_fragment_bytes(as,section_index,data,size,true)' not in body:
    raise SystemExit('machine CodeEmitter bypasses fragment-owned byte staging')
if 'minias_section_append(' in body:
    raise SystemExit('machine CodeEmitter directly materializes Section bytes')
start=s.index('static bool minias_stage_fragment_bytes(MiniAs *as,int section,')
end=s.index('static bool minias_stage_fragment_pattern(',start)
stage=s[start:end]
for invariant in (
    'MINIAS_WRITE_COPIED_BYTES',
    'result->byte_storage_size+=size',
    'result->byte_count+=size',
    'result->writes[result->write_count++]',
):
    if invariant not in stage:
        raise SystemExit('missing private ordered byte staging invariant: '+invariant)
if 'minias_section_append(' in stage or 'sec->size +=' in stage:
    raise SystemExit('private fragment staging mutated the final Section')
start=s.index('static bool minias_commit_encode_result(')
end=s.index('bool minias_emit_instruction_bytes(',start)
commit=s[start:end]
for invariant in (
    'result->byte_count != owner->size',
    'logical_size!=result->byte_count',
    'result->write_count',
    'minias_grow_array((void **)&as->fixups',
    'minias_record_fixup(as,',
    'minias_section_append(',
    'as->committing_fragment=true',
    'minias_encode_result_destroy(result)',
):
    if invariant not in commit:
        raise SystemExit('missing complete EncodeResult commit invariant: '+invariant)
if 'as->committing_fragment=false' not in commit:
    raise SystemExit('commit guard not released after final Section materialization')
start=s.index('bool minias_emit_sections(')
end=s.index('int minias_assemble_config(',start)
body=s[start:end]
if 'section->size = (size_t)stmt->offset' in body:
    raise SystemExit('MCFragment still restores a virtual Section cursor')
if 'as->active_encode_result->byte_count!=stmt->size' not in body:
    raise SystemExit('missing final encoded fragment size check')
if 'if(!minias_commit_encode_result(as,stmt))' not in body:
    raise SystemExit('MCFragment does not require successful Common commit')
print('MINIAS_MC_FRAGMENT_TRANSACTION=PASS target_cursor=isolated all_fragments=single_commit encoded_size=checked stage_guard=removed')
PY
