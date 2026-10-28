#!/usr/bin/env bash
# Semantic ownership: four target CodeEmitters must not advance Section
# cursors or write partial machine instructions into finalized Sections.
set -Eeuo pipefail
src=assembler/src/assembler.c
grep -q 'target-bypassed-mc-code-emitter' "$src"
grep -q 'encoded_instruction_size != stmt->size' "$src"
python3 - "$src" <<'PY'
import sys
s=open(sys.argv[1],encoding='utf-8').read()
start=s.index('bool minias_emit_instruction_bytes(')
end=s.index('bool minias_section_append(',start)
body=s[start:end]
if 'sec->size +=' in body or 'sec->size++' in body:
    raise SystemExit('machine CodeEmitter changed the final Section cursor')
start=s.index('bool minias_emit_sections(')
end=s.index('int minias_assemble_config(',start)
body=s[start:end]
if 'section->size = (size_t)stmt->offset' in body:
    raise SystemExit('MCFragment still restores a virtual Section cursor')
if 'minias_section_append(as,stmt->section,' not in body:
    raise SystemExit('missing atomic fragment commit')
print('MINIAS_MC_FRAGMENT_TRANSACTION=PASS target_cursor=isolated commit=atomic')
PY
