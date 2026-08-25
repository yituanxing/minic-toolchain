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
    old_guard = '''    if (!minic_c0_record_value_is_copy_source(context->body->program, expression_id) ||
        !minic_c0_record_value_is_address_backed(context->body->program, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    if segment.count(old_guard) != 1:
        raise SystemExit('record call argument legacy address-backed guard changed')
    brace = segment.find('{')
    if brace < 0:
        raise SystemExit('record call argument helper body missing')
    comment = '''
    /* M128_RECORD_CALL_ARGUMENT_MATERIALIZATION: a by-value record argument
       consumes a record rvalue, not merely an already-address-backed lvalue.
       Make lower_record_materialized_address() the single aggregate producer
       owner here: it handles conditionals, compound literals, direct record
       returns, and falls back fail-closed for ordinary address-backed values.
       The private argument object below remains the evaluation-order snapshot
       consumed by the existing Core/ABI OBJECT call-argument path. */'''
    segment = segment[:brace + 1] + comment + segment[brace + 1:]
    segment = segment.replace(old_guard, '', 1)
    segment = segment.replace(old_name, 'lower_record_materialized_address(', 1)
    text = text[:start] + segment + text[end:]
    core_path.write_text(text)

# Keep the existing broad record-conditional regression unchanged in legacy
# mode. Add one isolated strict-Core contract for exactly the M128 consumer:
# a record conditional, including a direct record-return producer, passed by
# value to a declared callee. Avoid unrelated if/compare/result-use lowering.
source_path = Path('tests/compiler/c0/record_call_argument_materialization.c')
source_text = '''typedef struct {
    unsigned long bits;
} record_word_t;

static record_word_t make_word(unsigned long value) {
    return (record_word_t){value + 1UL};
}

void consume_word(record_word_t value);

int main(void) {
    consume_word(0 ? (record_word_t){3UL} : make_word(10UL));
    return 0;
}
'''
source_path.write_text(source_text)

script_path = Path('tests/compiler/c0/run-record-conditional-materialization.sh')
script = script_path.read_text()
strict_marker = 'record_call_argument_materialization.c'
if strict_marker not in script:
    anchor = "printf '%s\\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument'"
    block = '''"$host_cc" -E -P -std=gnu11 -x c \\
    "$root/tests/compiler/c0/record_call_argument_materialization.c" \\
    -o "$work/call-argument.i"
MINIC_CORE_IR=strict "$minic" -S "$work/call-argument.i" \\
    -o "$work/call-argument.strict.s"
grep -F '.Lminic_record_cond_false_' "$work/call-argument.strict.s" >/dev/null
grep -F '  call consume_word' "$work/call-argument.strict.s" >/dev/null

printf '%s\\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument strict-call-argument=1' '''.rstrip()
    if anchor not in script:
        raise SystemExit('record conditional PASS anchor changed')
    script = script.replace(anchor, block, 1)
    script_path.write_text(script)

print('M128 by-value record call argument materialization and isolated strict regression staged')
