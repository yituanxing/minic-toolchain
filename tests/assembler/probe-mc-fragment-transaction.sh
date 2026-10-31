#!/usr/bin/env bash
# Semantic ownership: four target CodeEmitters must not advance Section
# cursors or write partial machine instructions into finalized Sections.
set -Eeuo pipefail
src=assembler/src/assembler.c
# The old capture_instruction_bytes switch was the two-commit-path bridge.
! grep -q 'capture_instruction_bytes' "$src"
grep -q 'minias_stage_fragment_bytes' "$src"
grep -q 'minias_stage_fragment_pattern' "$src"
python3 - "$src" assembler/src/mc_encode_result.c <<'PY'
import sys
s=open(sys.argv[1],encoding='utf-8').read()
mc=open(sys.argv[2],encoding='utf-8').read()
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
# Fixed MCData, instruction SymbolRef and absolute-zero relocation markers
# share the same EncodeResult append owner. Their semantic validity checks
# deliberately remain different, so this guards ownership, not ISA syntax.
start=mc.index('bool minias_encode_result_stage_fixup(')
end=mc.index('static bool stage_instruction_fixup(',start)
staging=mc[start:end]
if 'minias_grow_array((void **)&result->fixups' not in staging:
    raise SystemExit('missing single EncodeResult fixup vector owner')
start=s.index('static bool minias_stage_common_fixup(')
end=s.index('bool minias_add_fixup(',start)
data_stage=s[start:end]
if 'minias_encode_result_stage_fixup(as,&request,' not in data_stage:
    raise SystemExit('MCData bypasses common EncodeResult fixup owner')
if 'result->fixups[result->fixup_count++]' in data_stage:
    raise SystemExit('MCData has a duplicate staged Fixup append')
start=mc.index('static bool stage_instruction_fixup(')
end=mc.index('bool minias_encode_request_anchor_ref(',start)
target_stage=mc[start:end]
if target_stage.count('return stage_instruction_fixup(as,')!=2:
    raise SystemExit('Target SymbolRef/abs0 Fixup wrappers bypass shared validation')
if 'minias_encode_result_stage_fixup(' not in target_stage:
    raise SystemExit('Target Fixup staging bypasses common EncodeResult owner')
print('MINIAS_MC_FRAGMENT_TRANSACTION=PASS target_cursor=isolated all_fragments=single_commit encoded_size=checked stage_guard=removed fixup_stage_owner=single')
PY
