#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
text = core_path.read_text()
marker = 'M128_RECORD_CALL_ARGUMENT_MATERIALIZATION'
if marker not in text:
    start = text.find('static MinicCoreLowerStatus lower_record_call_argument_object(')
    if start < 0:
        raise SystemExit('record call argument helper missing')
    end = text.find('\nstatic ', start + 1)
    if end < 0:
        raise SystemExit('record call argument helper boundary missing')
    segment = text[start:end]
    old_name = 'lower_record_value_address('
    if segment.count(old_name) != 1:
        raise SystemExit(
            f'record call argument helper expected one address-only consumer, found {segment.count(old_name)}'
        )
    call_pos = segment.find(old_name)
    statement_pos = segment.rfind('    argument_status', 0, call_pos)
    if statement_pos < 0:
        raise SystemExit('record call argument status statement missing')
    comment = '''    /* M128_RECORD_CALL_ARGUMENT_MATERIALIZATION: a by-value record
       argument consumes a record rvalue, not merely an already-address-backed
       lvalue.  Use the same aggregate materialization seam as record
       assignment so conditionals, compound literals and direct record-return
       calls compose uniformly before the private argument copy is made. */
'''
    segment = segment[:statement_pos] + comment + segment[statement_pos:]
    segment = segment.replace(old_name, 'lower_record_materialized_address(', 1)
    text = text[:start] + segment + text[end:]
    core_path.write_text(text)

# Permanent strict regression already contains the exact generic shape:
# record conditional (compound literal / record-returning call) consumed as a
# by-value record argument. Promote that existing test to the strict Core IR
# route instead of adding a Linux-shaped special case.
script_path = Path('tests/compiler/c0/run-record-conditional-materialization.sh')
script = script_path.read_text()
if 'record-conditional-materialization.strict.s' not in script:
    anchor = '''"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F '.Lminic_record_cond_false_' "$work/output.s" >/dev/null
'''
    replacement = '''"$minic" -S "$work/input.i" -o "$work/output.s"
MINIC_CORE_IR=strict "$minic" -S "$work/input.i" \\
    -o "$work/record-conditional-materialization.strict.s"

grep -F '.Lminic_record_cond_false_' "$work/output.s" >/dev/null
grep -F '.Lminic_record_cond_false_' "$work/record-conditional-materialization.strict.s" >/dev/null
grep -F '  call consume' "$work/record-conditional-materialization.strict.s" >/dev/null
'''
    if anchor not in script:
        raise SystemExit('record conditional regression anchor changed')
    script = script.replace(anchor, replacement, 1)
    script = script.replace(
        "printf '%s\\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument'",
        "printf '%s\\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument strict-core=1'",
        1,
    )
    script_path.write_text(script)

print('M128 by-value record call argument materialization and strict regression staged')
