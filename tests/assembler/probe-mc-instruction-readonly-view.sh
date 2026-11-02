#!/usr/bin/env bash
# Enforce the Parse -> Measure/Relax/Encode instruction write boundary by
# compiling actual would-be Target mutations against the public target ABI.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import os
import re
import subprocess

root=Path('assembler/src')
api=(root/'minias_target_api.h').read_text()
core=(root/'mc_operand_semantics.c').read_text()
lowering=(root/'common_lowering.c').read_text()
layout=(root/'mc_layout.c').read_text()
emission=(root/'common_emission.c').read_text()
for fragment in ('MiniAsInstructionView','MiniAsMCInstView',
                 'MiniAsMCOperandView'):
    if 'typedef struct '+fragment+' {' not in api:
        raise SystemExit('Missing immutable instruction projection: '+fragment)
for signature in (
    'const MiniAsInstructionView *, uint32_t *);',
    'bool (*encode)(MiniAsEncodeContext *, const MiniAsInstructionView *);',
    'const MiniAsInstructionView *,\n                                        MiniAsTargetRelaxPlan *);'):
    if signature not in api:
        raise SystemExit('Old writable MC phase callback survives: '+signature)
if 'bool (*parse_mcinst)(MiniAsParseContext *, MiniAsParseInstruction *);' not in api:
    raise SystemExit('Parse lost its unique construction authority')
if 'typedef struct MiniAsStmt {' in api or \
   'MiniAsFragmentPayload' in api or \
   'typedef struct MiniAsStmt {' not in (root/'minias_fragment_internal.h').read_text():
    raise SystemExit('Raw Common Fragment escaped into Target-visible ABI')
for phase,source in (('Measure',lowering),('Relax',layout),('Encode',emission)):
    if 'minias_project_instruction(' not in source:
        raise SystemExit(phase+' bypasses the one Common instruction projector')
if core.count('bool minias_project_instruction(')!=1 or \
   'source->count > source->capacity' not in core or \
   'source->count > operand_capacity' not in core:
    raise SystemExit('One checked Common projector was removed or duplicated')
if 'dispose_target_payload' in api.split('typedef struct MiniAsMCInstView {')[1].split('} MiniAsMCInstView;')[0] or \
   'dispose_memory' in api.split('typedef struct MiniAsMCInstView {')[1].split('} MiniAsMCInstView;')[0]:
    raise SystemExit('Lifecycle mutation leaked into read-phase instruction')

for filename,functions in {
    'riscv_target.c':['riscv_plan_instruction_relaxation'],
    'riscv_encode.c':['minias_riscv_measure_mcinst','minias_riscv_encode'],
    'x86_64_target.c':['x64_measure','x64_encode',
                       'x64_plan_instruction_relaxation'],
    'aarch64_target.c':['aarch64_measure_mcinst','encode'],
    'loongarch64_target.c':['la_measure','la_encode'],
}.items():
    source=(root/filename).read_text()
    for name in functions:
        if not re.search(r'\b'+name+r'\s*\([^;{]*?\bconst MiniAsInstructionView\s*\*',
                         source,flags=re.DOTALL):
            raise SystemExit(filename+': '+name+' retains mutable instruction')

def reject(name,body,diagnostic):
    code='#include "minias_target_api.h"\nvoid invalid(const MiniAsInstructionView *v) { '+body+' }\n'
    result=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Werror',
                           '-fsyntax-only','-x','c','-Iassembler/src','-'],
                          input=code,text=True,capture_output=True)
    if result.returncode==0 or diagnostic not in result.stderr:
        raise SystemExit(name+': mutable nested view compiled:\n'+result.stderr)

reject('instruction size','v->size=4U;','read-only')
reject('MCInst opcode','v->inst->opcode=9U;','read-only')
reject('MCOperand register','v->inst->operands[0].reg=9U;','read-only')
reject('bound SymbolRef','v->inst->operands[0].bound_symbol->addend=1;','read-only')
reject('ISA memory', 'unsigned char *p=v->inst->operands[0].memory; (void)p;',
       'discards')
reject('ISA lowering plan','unsigned char *p=v->inst->target_payload; (void)p;',
       'discards')
reject('lifetime destructor','(void)v->inst->dispose_memory;',
       'no member')
reject('writable MCInst pointer',
       'MiniAsMCInst *p=v->inst; (void)p;', 'incompatible')
print('MINIAS_MCINST_READONLY_COMPILE=PASS callbacks=4 targets=5 '
      'negative_mutations=8 projector=single')
PY

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${CC:-cc}" -Iassembler/src -std=c11 -Wall -Wextra -Wpedantic -Werror \
  assembler/src/mc_operand_semantics.c \
  tests/assembler/mcinst-instruction-view-contract.c \
  -o "$work/instruction-projection"
"$work/instruction-projection"
