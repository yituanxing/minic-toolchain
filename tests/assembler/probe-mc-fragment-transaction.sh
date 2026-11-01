#!/usr/bin/env bash
# Semantic ownership: four target CodeEmitters must not advance Section
# cursors or write partial machine instructions into finalized Sections.
set -Eeuo pipefail
src=assembler/src/assembler.c
# The old capture_instruction_bytes switch was the two-commit-path bridge.
! grep -q 'capture_instruction_bytes' "$src"
grep -q 'minias_stage_fragment_bytes' "$src"
grep -q 'minias_stage_fragment_pattern' "$src"
python3 - "$src" assembler/src/mc_encode_result.c assembler/src/minias_internal.h <<'PY'
import sys
s=open(sys.argv[1],encoding='utf-8').read()
mc=open(sys.argv[2],encoding='utf-8').read()
h=open(sys.argv[3],encoding='utf-8').read()
start=s.index('bool minias_emit_instruction_bytes(')
end=s.index('bool minias_section_append(',start)
body=s[start:end]
if 'sec->size +=' in body or 'sec->size++' in body:
    raise SystemExit('machine CodeEmitter changed the final Section cursor')
if 'minias_stage_fragment_bytes(as,section_index,data,size,true)' not in body:
    raise SystemExit('machine CodeEmitter bypasses fragment-owned byte staging')
if 'section_index=owner->section;' not in body:
    raise SystemExit('CodeEmitter Section not derived from active Fragment')
start=h.index('bool minias_emit_instruction_bytes(')
end=h.index(');',start)
if 'int section' in h[start:end] or 'section_index' in h[start:end]:
    raise SystemExit('byte-emission API still exposes a Section selector')
for target_file in ('aarch64_target.c','x86_64_target.c',
                    'loongarch64_target.c','riscv_encode.c','riscv_target.c'):
    code=open('assembler/src/'+target_file,encoding='utf-8').read()
    import re
    if re.search(r'minias_emit_instruction_bytes\s*\(\s*as\s*,'
                 r'\s*(?:section|section_index|stmt->section|s->section)\s*,',code):
        raise SystemExit('Target can still select output Section: '+target_file)
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
# All MCData and Target Fixups use one private EncodeResult vector owner.
# The old Common-side request staging and raw exposed append are gone.
if 'minias_encode_result_stage_fixup' in h:
    raise SystemExit('raw EncodeResult append exposed in internal header')
if 'minias_stage_common_fixup' in s:
    raise SystemExit('legacy Common MCData request staging still present')
if 'bool minias_add_fixup(' in s or 'bool minias_add_fixup_symbol_id(' in s:
    raise SystemExit('Common still duplicates semantic MCData staging APIs')
start=mc.index('static bool minias_encode_result_stage_fixup(')
end=mc.index('bool minias_fixup_field_width(',start)
staging=mc[start:end]
if 'minias_grow_array((void **)&result->fixups' not in staging:
    raise SystemExit('private EncodeResult Fixup staging owner missing')
if 'result->fixups[result->fixup_count++]' not in staging:
    raise SystemExit('ordered Fixup vector append owner missing')
start=mc.index('static bool minias_stage_common_fixup(')
end=mc.index('bool minias_add_fixup(',start)
data_stage=mc[start:end]
if 'minias_encode_result_stage_fixup(as,&request,' not in data_stage:
    raise SystemExit('MCData no longer uses private EncodeResult Fixup owner')
if 'result->fixups[result->fixup_count++]' in data_stage:
    raise SystemExit('MCData reimplements raw Fixup-vector append')
if 'bool minias_add_fixup_symbol_id(' not in mc:
    raise SystemExit('moved semantic MCData wrappers missing')
# MCData Fixup Section is owned by the active Fragment, not chosen by its
# caller. Keep the semantic name/SymbolID entry points, but no selector.
for signature in ('minias_add_fixup', 'minias_add_fixup_symbol_id'):
    start=h.index('bool '+signature+'(')
    stop=h.index(');',start)
    if 'int section' in h[start:stop]:
        raise SystemExit('MCData Fixup still exposes Section selector: '+signature)
if 'section=owner->section;' not in data_stage:
    raise SystemExit('MCData Fixup Section is not derived from active Fragment')
# Neither staged Fixup nor anchor may cache an independently mutable
# Section/subsection/position; Common derives them from the commit owner.
start=h.index('typedef struct MiniAsEncodeFixupRequest {')
end=h.index('} MiniAsEncodeFixupRequest;',start)
if 'int section;' in h[start:end]:
    raise SystemExit('staged Fixup retains a redundant Section')
start=h.index('typedef struct MiniAsEncodeAnchorRequest {')
end=h.index('} MiniAsEncodeAnchorRequest;',start)
if any(field in h[start:end] for field in ('int section;',
                                           'uint32_t subsection;',
                                           'uint64_t offset;')):
    raise SystemExit('staged anchor retains redundant Fragment position')
if 'minias_record_fixup(as, owner->section,' not in s:
    raise SystemExit('Fixup commit is not bound to active Fragment Section')
# A Target cannot choose another Section or Fragment during Fixup staging:
# its output capability is bound to the active EncodeResult owner.
if 'section=owner->section;' not in mc:
    raise SystemExit('encoded Fixup section is not derived from owner')
if 'stmt=&as->stmts[as->emitting_fragment_id];' not in mc:
    raise SystemExit('encoded anchor owner is not derived from active fragment')
for signature in ('minias_encode_add_fixup_ref','minias_encode_add_fixup_abs0'):
    start=h.index('bool '+signature+'(')
    stop=h.index(');',start)
    if 'int section' in h[start:stop]:
        raise SystemExit('Target can still select Fixup section: '+signature)
anchor_start=h.index('bool minias_encode_request_anchor_ref(')
anchor_end=h.index(');',anchor_start)
if 'MiniAsStmt' in h[anchor_start:anchor_end]:
    raise SystemExit('Target can still supply arbitrary anchor Fragment')
for target_path in ('aarch64_target.c','x86_64_target.c',
                    'loongarch64_target.c','riscv_encode.c'):
    code=open('assembler/src/'+target_path,encoding='utf-8').read()
    import re
    if re.search(r'minias_encode_add_fixup_(ref|abs0)\s*\(\s*as\s*,'
                 r'\s*(s|stmt)->section\s*,',code):
        raise SystemExit('legacy Target-controlled Fixup section: '+target_path)
    if re.search(r'minias_encode_request_anchor_ref\s*\(\s*as\s*,'
                 r'\s*stmt\s*,',code):
        raise SystemExit('legacy Target-controlled anchor Fragment: '+target_path)
start=mc.index('static bool stage_instruction_fixup(')
end=mc.index('bool minias_encode_request_anchor_ref(',start)
target_stage=mc[start:end]
if target_stage.count('return stage_instruction_fixup(as,')!=2:
    raise SystemExit('Target SymbolRef/abs0 Fixup wrappers bypass shared validation')
if 'minias_encode_result_stage_fixup(' not in target_stage:
    raise SystemExit('Target Fixup staging bypasses common EncodeResult owner')
print('MINIAS_MC_FRAGMENT_TRANSACTION=PASS target_cursor=isolated all_fragments=single_commit encoded_size=checked stage_guard=removed fixup_stage_owner=single')
PY
