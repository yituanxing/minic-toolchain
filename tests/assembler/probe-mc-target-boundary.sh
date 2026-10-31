#!/usr/bin/env bash
# Guard the actual boundary: ISA-private fixup kinds and machine-code emit
# callbacks cannot leak into MC Core, ELF serialization, or the CLI.
set -Eeuo pipefail
if grep -Eq 'MINIAS_FIXUP_(RV_|A64_|X64_|LARCH_)' \
    assembler/src/minias_internal.h assembler/src/assembler.c \
    assembler/src/elf_writer.c; then
    echo 'Target FixupKind leaked into MC Core' >&2
    exit 1
fi
for backend in riscv_target.c riscv_encode.c aarch64_target.c \
               x86_64_target.c loongarch64_target.c; do
    if test -f "assembler/src/$backend" && \
       grep -q 'minias_section_append(as,' "assembler/src/$backend"; then
        echo "Direct Section append in target encoder: $backend" >&2
        exit 1
    fi
done
# A byte-only capture is not an MC transaction.  Every Target now returns
# fragment-owned Fixup/Symbol requests to the Common encoder transaction;
# legacy direct edits of the global object graph are forbidden.  Inspect
# tokens rather than raw grep hits so comments cannot manufacture a PASS.
python3 - <<'PY'
from pathlib import Path
import re

targets = ('riscv_target.c', 'riscv_encode.c', 'aarch64_target.c',
           'x86_64_target.c', 'loongarch64_target.c')
bad = []
patterns = {
    'direct Fixup insertion': r'\bminias_add_fixup\s*\(',
    'global Fixup array access': r'\bas\s*->\s*fixups\s*\[',
    'global ELF flag mutation':
        r'\bas\s*->\s*elf_flags\s*(?:\|=|&=|\^=|\+=|-=|=(?!=)|\+\+|--)',
    'global synthetic-anchor counter mutation':
        r'\bas\s*->\s*pcrel_anchor_counter\s*(?:\+\+|--|\+=|-=|=(?!=))',
    'direct synthetic Symbol insertion':
        r'\bminias_get_symbol\s*\(\s*as\s*,[^;]*?,\s*true\s*\)',
    'Target bypasses Common symbol-reference lookup':
        r'\bminias_get_symbol\s*\(',
    'legacy name-based target Fixup request':
        r'\bminias_encode_add_fixup\s*\(',
    'legacy symbolic MCOperand representation':
        r'\bMINIAS_MC_SYMBOL(?:_DIFFERENCE)?\b',
}
for name in targets:
    path = Path('assembler/src') / name
    source = path.read_text(encoding='utf-8')
    # Drop C comments and literals without changing line breaks, then check
    # whole tokens across whitespace/newlines.  The functional test below is
    # a separate gate: source spelling alone is not semantic proof.
    cleaned = re.sub(
        r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m: '\n' * m.group().count('\n') + ' ', source)
    for label, pattern in patterns.items():
        match = re.search(pattern, cleaned, flags=re.DOTALL)
        if match:
            line = cleaned.count('\n', 0, match.start()) + 1
            bad.append(f'{path}:{line}: {label}')
if bad:
    raise SystemExit('MiniAS Target/Common encode ownership violation:\n' +
                     '\n'.join(bad))

core = Path('assembler/src/minias_internal.h').read_text(encoding='utf-8')
for api in ('minias_encode_add_elf_flags', 'minias_encode_add_fixup_ref',
            'minias_encode_request_anchor_ref', 'minias_bind_symbol_ref',
            'minias_symbol_ref_lookup'):
    if not re.search(r'\b' + api + r'\s*\(', core):
        raise SystemExit('Missing Common transaction API: ' + api)
for legacy in ('minias_encode_add_fixup', 'minias_encode_request_anchor',
               'has_symbol_ref'):
    if re.search(r'\b' + legacy + r'\b', core):
        raise SystemExit('Legacy dual-track transaction API remains: ' + legacy)
print('MINIAS_MC_ENCODE_BOUNDARY=PASS target_mutation=none requests=typed')
PY
# A post-emit validation hook must not hide object mutations. AArch64 and RV
# add mapping symbols through an explicit finalization stage, while LoongArch
# retains its separate validation. The writer runs after both phases.
python3 - <<'PY'
from pathlib import Path
import re

core = Path('assembler/src/minias_internal.h').read_text()
driver = Path('assembler/src/assembler.c').read_text()
targets = {
    'aarch64_target.c': ('validate', 'a64_record_source_mappings'),
    'riscv_target.c': ('strict_base_i_check_sections',
                       'riscv_record_object_mapping_symbols'),
}
if not re.search(r'bool\s*\(\*validate_output\)\s*\(const MiniAsPostEmitView\s*\*', core):
    raise SystemExit('Target validation still receives a writable Core')
if not re.search(r'bool\s*\(\*finalize_output\)\s*\(const MiniAsPostEmitView\s*\*', core):
    raise SystemExit('Target finalization still receives a writable Core')
steps = ('minias_emit_sections(&as)',
         'as.target->validate_output(&final_layout, &errors)',
         'minias_finalize_target_output(&as,&final_layout,&errors,&object)',
         'minias_write_elf64(&as, output_path)')
offsets = [driver.find(step) for step in steps]
if -1 in offsets or offsets != sorted(offsets):
    raise SystemExit('Target validation/finalization/ELF output order drifted')
if not re.search(r'as->finalizing_target_output\s*=\s*true;\s*'
                 r'ok\s*=\s*as->target->finalize_output\(layout,errors,object\);\s*'
                 r'as->finalizing_target_output\s*=\s*false;', driver):
    raise SystemExit('Target finalization capability guard is missing')
for filename in ('assembler.c', 'cfi_writer.c'):
    source = (Path('assembler/src') / filename).read_text()
    if re.search(r'as->fixups\[as->fixup_count\s*-\s*1U?\]', source):
        raise SystemExit(f'{filename}: post-creation Fixup mutation remains')
a64 = (Path('assembler/src') / 'aarch64_target.c').read_text()
planner = a64.split('static bool a64_record_source_mappings(', 1)[1].split(
    'static bool validate(', 1)[0]
if 'stmt->op' in planner or 'section->align=' in planner:
    raise SystemExit('AArch64 finalizer reopens source or writes Common Section')
if 'A64_SITE_INST_DATA' not in planner or \
   'A64_SITE_EXPLICIT_ALIGN' not in planner or \
   'minias_post_emit_raise_section_alignment' not in planner:
    raise SystemExit('AArch64 finalizer omitted typed intent or Common alignment API')
if 'return minias_require_target_section_alignment(sink->as, section, align);' not in driver:
    raise SystemExit('Post-emit sink bypassed guarded Common alignment owner')
for target, (validator, finalizer) in targets.items():
    source = (Path('assembler/src') / target).read_text()
    for hook, function in (('validate_output', validator),
                           ('finalize_output', finalizer)):
        if not re.search(r'\.' + hook + r'\s*=\s*' + function + r'\s*[,}]', source):
            raise SystemExit(f'{target}: missing {hook}={function}')
print('MINIAS_MC_OBJECT_FINALIZATION=PASS validation_before_mapping_before_writer=2')
PY
# A post-emit Target gets only immutable layout slices and an opaque error
# handle. Only the separately typed finalization sink can request object
# symbols/alignment from Common, not the read-only validator.
python3 - <<'PY'
from pathlib import Path
import re

core = Path('assembler/src/minias_internal.h').read_text()
driver = Path('assembler/src/assembler.c').read_text()
a64 = Path('assembler/src/aarch64_target.c').read_text()
rv = Path('assembler/src/riscv_target.c').read_text()
la = Path('assembler/src/loongarch64_target.c').read_text()
view = core.split('typedef struct MiniAsPostEmitView {', 1)[1].split('} MiniAsPostEmitView;', 1)[0]
if re.search(r'\bMiniAs\s*\*|\bMiniAsSymbol\s*\*|\bMiniAsFixup\s*\*', view):
    raise SystemExit('Post-emit view exposes a mutable Common object')
if 'struct MiniAsPostEmitObjectSink { MiniAs *as; };' not in driver or \
        'struct MiniAsPostEmitObjectSink {' in core:
    raise SystemExit('Post-emit mutation handle is not Common-private')
for source, fn in ((a64, 'a64_record_source_mappings'),
                   (rv, 'riscv_record_object_mapping_symbols')):
    if not re.search(r'static bool\s+' + fn +
                     r'\s*\(\s*const MiniAsPostEmitView\s*\*', source):
        raise SystemExit(fn + ' still receives a mutable Core')
    hook_body = source.split('static bool ' + fn + '(', 1)[1]
    hook_body = hook_body.split('\nstatic ', 1)[0]
    if 'as->' in hook_body or 'minias_set_error(as,' in hook_body:
        raise SystemExit(fn + ' still reaches into the mutable Core')
if 'minias_post_emit_raise_section_alignment(object,' not in a64 or \
        'minias_post_emit_define_mapping_symbol(object,' not in a64 or \
        'minias_post_emit_define_mapping_symbol(' not in rv:
    raise SystemExit('Target object mutation bypassed the finalization sink')
if not re.search(r'static bool la_validate\s*\(const MiniAsPostEmitView\s*\*', la):
    raise SystemExit('LoongArch output validator still receives writable Core')
print('MINIAS_MC_POST_EMIT_VIEW=PASS final_layout=readonly object_mutation=Common_sink')
PY
# Typed instruction parsing may request Common symbol binding, current
# expression evaluation and AArch64 literal pooling, but must never receive
# writable MiniAs itself. The sole Common parse context owns those services.
python3 - <<'PY'
from pathlib import Path
import re

root = Path('assembler/src')
core = (root / 'minias_internal.h').read_text()
driver = (root / 'assembler.c').read_text()
if not re.search(r'typedef struct MiniAsParseContext MiniAsParseContext;', core):
    raise SystemExit('Missing opaque parse capability')
if 'struct MiniAsParseContext {' in core or \
        'struct MiniAsParseContext { MiniAs *as; };' not in driver:
    raise SystemExit('Parse context exposed its mutable Common owner')
if not re.search(r'bool\s*\(\*parse_mcinst\)\s*\('
                 r'MiniAsParseContext\s*\*\s*,\s*MiniAsStmt\s*\*', core):
    raise SystemExit('Target parser still receives full mutable MiniAs')
if 'MiniAsParseContext parsing = { .as=as };' not in driver or \
        'as->target->parse_mcinst(&parsing, st)' not in driver:
    raise SystemExit('Common failed to provide its single parse owner')
required = ('target_state', 'is_elf32', 'has_error', 'error',
            'bind_symbol_ref', 'bind_symbol_difference_ref',
            'symbol_pair_same_defined_section', 'eval_absolute_data_bits',
            'evaluate_absolute_operand',
            'eval_symbol_difference_absolute_tail', 'queue_literal')
for name in required:
    if not re.search(r'\bminias_parse_' + name + r'\s*\(', core):
        raise SystemExit('Missing parse-specific Common service: ' + name)
for filename, fn in (('riscv_encode.c', 'minias_riscv_parse_mcinst'),
                     ('aarch64_target.c', 'parse_mcinst'),
                     ('x86_64_target.c', 'x64_parse'),
                     ('loongarch64_target.c', 'la_parse')):
    source = (root / filename).read_text()
    match = re.search(r'\bbool\s+' + fn +
                      r'\s*\(\s*MiniAsParseContext\s*\*', source)
    if not match:
        raise SystemExit(f'{filename}: parser still accepts mutable Common')
    end = source.find('\n}', match.end())
    if end < 0 or 'as->' in source[match.end():end]:
        raise SystemExit(f'{filename}: parser still directly reads Common')
print('MINIAS_MC_PARSE_SERVICES=PASS targets=4 single_owner=Common no_legacy_callback=1')
PY
# Source-site ISA snapshots and code-alignment defaults are pure Target-state
# queries. Common passes only opaque const Target context, never its mutable
# Symbol/Section/SourceGraph owner. All three nontrivial callbacks migrate
# together; LoongArch correctly retains the NULL default.
python3 - <<'PY'
from pathlib import Path
import re

root = Path('assembler/src')
core = (root / 'minias_internal.h').read_text()
driver = (root / 'assembler.c').read_text()
if not re.search(r'void\s*\(\*capture_site_state\)\s*\('
                 r'const void\s*\*target_state\s*,', core):
    raise SystemExit('Source-site snapshot still receives Common MiniAs')
if not re.search(r'unsigned\s*\(\*default_exec_alignment_for_site\)\s*\('
                 r'const void\s*\*target_state\s*\)', core):
    raise SystemExit('Executable alignment still receives Common MiniAs')
for file, fn in (('aarch64_target.c', 'a64_capture_site_state'),
                 ('riscv_target.c', 'riscv_capture_site_state'),
                 ('x86_64_target.c', 'x64_capture_site_state')):
    source = (root / file).read_text()
    match = re.search(r'static void\s+' + fn +
                      r'\s*\(\s*const void\s*\*target_state\s*,', source)
    if not match:
        raise SystemExit(f'{file}: source-site capture retains Common root')
    body = source[match.end():source.find('\n}', match.end())]
    if 'as->' in body or 'MINIAS_RV_STATE(as)' in body:
        raise SystemExit(f'{file}: site capture reads Common state')
rv = (root / 'riscv_target.c').read_text()
if not re.search(r'static unsigned\s+riscv_default_exec_alignment_for_site'
                 r'\s*\(\s*const void\s*\*target_state\s*\)', rv):
    raise SystemExit('RISC-V execution alignment still reads Common')
if 'capture_site_state(as->target_state, st, &st->site)' not in driver or \
        'default_exec_alignment_for_site(as->target_state)' not in driver:
    raise SystemExit('Common did not pass private Target context')
print('MINIAS_MC_SITE_TARGET_STATE=PASS capture=3 alignment=1 common_root=none')
PY
# Instruction measurement runs after parse and before fragment placement.
# Four Targets share exactly one read-only callback; no measurement hook may
# borrow writable MiniAs, and sizing must reuse the same Target alternatives
# subsequently used by encoding (RVC words and x86 legacy prefixes).
python3 - <<'PY'
from pathlib import Path
import re

root = Path('assembler/src')
core = (root / 'minias_internal.h').read_text()
driver = (root / 'assembler.c').read_text()
view = core.split('typedef struct MiniAsMeasureView {', 1)[1].split(
    '} MiniAsMeasureView;', 1)[0]
if re.search(r'\bMiniAs\s*\*|\bMiniAsStmt\s*\*|\bMiniAsSection\s*\*', view):
    raise SystemExit('Measurement view exposes writable Common state')
if not re.search(r'bool\s*\(\*measure_mcinst\)\s*\('
                 r'const MiniAsMeasureView\s*\*\s*,\s*'
                 r'MiniAsMeasureErrorSink\s*\*', core):
    raise SystemExit('Measurement callback still borrows mutable MiniAs')
if 'struct MiniAsMeasureErrorSink { MiniAs *as; };' not in driver or \
        'struct MiniAsMeasureErrorSink {' in core:
    raise SystemExit('Measurement diagnostic sink must remain Common-private')
if 'const MiniAsMeasureView measure = { .target_state=as->target_state };' not in driver:
    raise SystemExit('Measurement view does not borrow Target state at parse site')
targets = {
    'aarch64_target.c': 'aarch64_measure_mcinst',
    'loongarch64_target.c': 'la_measure',
    'x86_64_target.c': 'x64_measure',
    'riscv_encode.c': 'minias_riscv_measure_mcinst',
}
for file, fn in targets.items():
    source = (root / file).read_text()
    if not re.search(r'\bbool\s+' + fn +
                     r'\s*\(\s*const MiniAsMeasureView\s*\*', source):
        raise SystemExit(f'{file}: measurement still takes mutable Core')
rv = (root / 'riscv_encode.c').read_text()
if rv.count('rv_rvc_encode_plain(state,stmt,&compressed)') != 1 or \
        'rv_rvc_encode_plain(MINIAS_RV_STATE(as),stmt,&compressed)' not in rv:
    raise SystemExit('RVC measure/encode diverged from their common plan')
x86 = (root / 'x86_64_target.c').read_text()
if x86.count('x64_legacy_mode_prefixes(') != 4 or \
        'x64_legacy_mode_prefixes(s,prefix,&n)' not in x86:
    raise SystemExit('x86 prefix size and emission diverged')
print('MINIAS_MC_MEASURE_VIEW=PASS targets=4 common_writable_state=none')
PY
# Only RV/x86 need ISA branch/operand relaxation. They receive an opaque
# Common query/diagnostic capability; encoding reuses the same target-private
# distance algorithm with its current-layout symbol, not a second planner.
python3 - <<'PY'
from pathlib import Path
import re

root = Path('assembler/src')
core = (root / 'minias_internal.h').read_text()
layout = (root / 'mc_layout.c').read_text()
rv = (root / 'riscv_target.c').read_text()
rv_encoder = (root / 'riscv_encode.c').read_text()
x86 = (root / 'x86_64_target.c').read_text()
if not re.search(r'bool\s*\(\*plan_instruction_relaxation\)\s*\('
                 r'MiniAsRelaxContext\s*\*,', core):
    raise SystemExit('Target relaxation callback still accepts writable Common')
if 'struct MiniAsRelaxContext {' in core or \
        'struct MiniAsRelaxContext { MiniAs *as; };' not in layout:
    raise SystemExit('Relax context must keep Common owner opaque to Targets')
for service in ('symbol_ref_lookup', 'resolve_symbol_difference_ref', 'error'):
    if not re.search(r'\bminias_relax_' + service + r'\s*\(', core):
        raise SystemExit('Missing Common relaxation service: ' + service)
if 'plan_instruction_relaxation(&context,stmt,plan)' not in layout:
    raise SystemExit('Common MCLayout bypassed its phase-limited context')
for source, name in ((rv, 'riscv_plan_instruction_relaxation'),
                     (x86, 'x64_plan_instruction_relaxation')):
    if not re.search(r'static bool\s+' + name +
                     r'\s*\(MiniAsRelaxContext\s*\*', source):
        raise SystemExit(name + ' still borrows mutable Common')
if not re.search(r'x64_branch_distance\s*\(const MiniAsSymbol\s*\*', x86):
    raise SystemExit('x86 relaxation and encoding lost shared pure distance')
if 'minias_riscv_compute_branch_plan(' not in rv_encoder or \
        'minias_riscv_compute_branch_plan(' not in rv:
    raise SystemExit('RV relaxation and encoding no longer share one branch policy')
if re.search(r'\bminias_riscv_resolve_branch_plan\s*\(', rv + rv_encoder):
    raise SystemExit('Legacy broad RV branch resolver remains')
print('MINIAS_MC_RELAX_SERVICES=PASS targets=2 common_root=opaque shared_branch_policy=1')
PY
# Full-source LoongArch relaxability is a necessary forward-looking policy.
# It must be precomputed once from a read-only completed SourceGraph, not
# lazily reconstructed by scanning mutable Common Fragments during encode.
python3 - <<'PY'
from pathlib import Path
import re

core = Path('assembler/src/minias_internal.h').read_text()
driver = Path('assembler/src/assembler.c').read_text()
la = Path('assembler/src/loongarch64_target.c').read_text()
if 'typedef struct MiniAsPreLayoutView {' not in core or \
        not re.search(r'bool\s*\(\*prepare_layout\)\s*\(const MiniAsPreLayoutView\s*\*', core):
    raise SystemExit('Target layout policy lacks a read-only parsed-source view')
steps = ('parse_loaded_source_graph(&as,&graph)',
         'as.target->prepare_layout(&parsed_fragments,',
         'minias_solve_fragment_layout(')
positions = [driver.find(step) for step in steps]
if -1 in positions or positions != sorted(positions):
    raise SystemExit('Target prepare-layout hook did not precede MCLayout')
if '.prepare_layout=la_prepare_layout' not in la or \
        'view->stmts[i]' not in la:
    raise SystemExit('LoongArch did not precompute section relaxability from view')
encoder = la.split('static bool la_section_is_linker_relaxable(', 1)[1]
encoder = encoder.split('static bool la_measure(', 1)[0]
if 'as->stmts' in encoder or 'calloc(' in encoder:
    raise SystemExit('LoongArch encoder still rescans mutable Common Fragments')
print('MINIAS_MC_PRE_LAYOUT_VIEW=PASS LoongArch_forward_section_policy=Target_owned_once')
PY
# Object ABI policy must not fall back from a rejected i386 request to an
# AMD64-wide semantic Fixup capability or relocation number.
python3 - <<'PY'
from pathlib import Path
import re

root = Path('assembler/src')
header = (root / 'minias_internal.h').read_text()
for file in ('minias_internal.h', 'mc_expr.c', 'elf_writer.c', 'x86_64_target.c'):
    source = (root / file).read_text()
    if 'supports_fixup_for_object' in source or \
       'fixup_relocation_type_for_object' in source:
        raise SystemExit(f'{file}: duplicate generic/object Fixup policy remains')
if not re.search(r'bool\s*\(\*supports_fixup\)\s*\('
                 r'const\s+MiniAs\s*\*', header):
    raise SystemExit('Fixup capability lost Object ABI profile')
print('MINIAS_MC_FIXUP_ABI=PASS policy=single_object_aware_interface')
PY
# Parse-site AArch64 mapping semantics must not be recovered by comparing
# source strings during late section finalization. RISC-V's RVE ABI must not
# be inferred from mutable Common ELF e_flags while parsing MC operands.
python3 - <<'PY'
from pathlib import Path

a64 = Path('assembler/src/aarch64_target.c').read_text()
late = a64.split('static bool a64_record_source_mappings(', 1)[1]
late = late.split('static bool aarch64_resolve_profile(', 1)[0]
if 'strcmp(stmt->op,' in late:
    raise SystemExit('AArch64 still reinterprets source directive text after layout')
if 'A64_SITE_INST_DATA' not in late or 'A64_SITE_EXPLICIT_ALIGN' not in late:
    raise SystemExit('AArch64 mapping did not consume typed parse-site provenance')
rv_encoder = Path('assembler/src/riscv_encode.c').read_text()
if 'as->elf_flags' in rv_encoder:
    raise SystemExit('RISC-V MC parser still reads Common ELF output flags')
if 'state->rv32e_abi' not in rv_encoder:
    raise SystemExit('RISC-V MC parser lost the target-owned E ABI gate')
print('MINIAS_MC_SITE_PROVENANCE=PASS AArch64_late_semantics=typed RV_E_ABI=target_owned')
PY
# Code alignment is a Target-owned NOP policy but still emits through the
# identical MC byte transaction as every instruction. No architecture gets
# a special direct-Section bridge for ALIGN or for an instruction emitter.
grep -q 'return minias_emit_instruction_bytes(as, section, bytes, sizeof(bytes));' \
    assembler/src/aarch64_target.c
grep -q 'minias_emit_instruction_bytes' assembler/src/riscv_target.c
grep -q 'minias_emit_instruction_bytes' assembler/src/x86_64_target.c
grep -q 'minias_emit_instruction_bytes' assembler/src/loongarch64_target.c
# Each Target must reserve a separate kind namespace.
for header in riscv_fixup_kinds.h aarch64_fixup_kinds.h \
              x86_64_fixup_kinds.h loongarch64_fixup_kinds.h; do
    test -s "assembler/src/$header"
done
grep -q 'minias_emit_instruction_bytes' assembler/src/assembler.c
grep -q 'MiniAsTargetSiteState' assembler/src/minias_internal.h
# LLVM MC-style lexical ownership: balanced operand grouping lives once in
# Common MC. All four Target parsers consume it, and retain ISA legality.
test -s assembler/src/mc_lexer.c
for target in riscv_encode.c aarch64_target.c x86_64_target.c \
              loongarch64_target.c; do
    if ! grep -q 'minias_split_mc_operands' "assembler/src/$target"; then
        echo "Target bypasses shared MC operand lexer: $target" >&2
        exit 1
    fi
done
if grep -q 'comparator_as' assembler/src/debug_line.c; then
    echo 'DWARF line comparator has global assembly context' >&2
    exit 1
fi
if grep -Eq '(strict_i_xlen|strict_rv64i|isa_explicit|isa_features|target_attributes|target_option_stack|target_option_depth)' assembler/src/minias_internal.h; then
    echo 'RISC-V profile or option stack leaked into MC Core' >&2
    exit 1
fi
# ISA parser options live in each TargetState and each retained statement's
# immutable target site snapshot.  A shared mutable target_options bitfield
# lets an unrelated Target silently inherit one ISA's option semantics.
# profile_features was an unused second site-state dimension: do not restore
# it alongside the legitimate target-owned site.mode_bits.
python3 - <<'PY'
from pathlib import Path
import re
for filename in ('assembler/src/minias_internal.h',
                 'assembler/src/assembler.c'):
    source = Path(filename).read_text(encoding='utf-8')
    cleaned = re.sub(
        r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        ' ', source)
    for legacy in ('target_options', 'profile_features'):
        if re.search(r'\b' + legacy + r'\b', cleaned):
            raise SystemExit(f'{filename}: Common still owns {legacy}')
PY
grep -q 'uint32_t mode_bits;' assembler/src/minias_internal.h
# AT&T base/index/SIB/FS/GS is not a cross-ISA MC addressing contract.
# Memory stays a semantic operand kind, while its data layout and nested
# allocations are private to the owning Target.
if grep -Eq 'MiniAsMCMemory|has_index|pc_relative|address_space' \
    assembler/src/minias_internal.h; then
    echo 'x86 memory addressing representation leaked into MC Core' >&2
    exit 1
fi
if grep -Eq 'MiniAsMCMemory|->memory->' \
    assembler/src/minias_internal.h assembler/src/mc_operand_semantics.c; then
    echo 'Common destructor depends on an ISA memory payload' >&2
    exit 1
fi
grep -q 'void (\*dispose_memory)(void \*);' assembler/src/minias_internal.h
grep -q 'typedef struct X64Memory' assembler/src/x86_64_target.c
grep -q 'm->dispose_memory=x64_dispose_memory;' assembler/src/x86_64_target.c
echo 'MINIAS_MC_TARGET_BOUNDARY=PASS ISA_fixups=private target_bytes=fragment_owned site=immutable shared_lexer=4 DWARF_sort=reentrant memory_payload=target_owned'
