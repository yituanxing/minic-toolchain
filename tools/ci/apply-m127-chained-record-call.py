#!/usr/bin/env python3
from pathlib import Path

# Production semantic seam.
path = Path('src/core/core_lower.c')
text = path.read_text()
old = '''        status = lower_record_value_address(
            context, expression->value.binary.right, &source_address);
'''
new = '''        /* M127_CHAINED_RECORD_MATERIALIZED_RHS: the value of a record
           assignment is the fully evaluated RHS snapshot.  Route the RHS
           through the materializing aggregate seam so direct record-returning
           calls, record conditionals and compound literals compose with
           chained assignment exactly like ordinary address-backed records. */
        status = lower_record_materialized_address(
            context, expression->value.binary.right, &source_address);
'''
marker = 'M109_CHAINED_RECORD_ASSIGNMENT_VALUE'
pos = text.find(marker)
if pos < 0:
    raise SystemExit('M109 chained record assignment seam missing')
if 'M127_CHAINED_RECORD_MATERIALIZED_RHS' not in text:
    call_pos = text.find(old, pos)
    if call_pos < 0:
        raise SystemExit('M109 RHS record-value call changed')
    next_marker = text.find('if (!minic_c0_record_value_is_address_backed', pos)
    if next_marker < 0 or call_pos > next_marker:
        raise SystemExit('M109 RHS replacement escaped assignment branch')
    text = text[:call_pos] + text[call_pos:].replace(old, new, 1)
    path.write_text(text)

# Permanent semantic regression: record-returning call -> inner assignment
# value -> outer record assignment.
test_path = Path('tests/compiler/c0/record_assignment_expression.c')
test = test_path.read_text()
anchor = '''union Payload {
    long wide;
    int words[2];
};
'''
addition = '''union Payload {
    long wide;
    int words[2];
};

struct Stamp {
    long sec;
    long nsec;
};

struct Holder {
    struct Stamp first;
    struct Stamp second;
};

extern struct Stamp make_stamp(long value);

void assign_both(struct Holder *holder, long value) {
    holder->first = holder->second = make_stamp(value);
}
'''
if 'void assign_both(struct Holder *holder, long value)' not in test:
    if anchor not in test:
        raise SystemExit('record assignment regression anchor changed')
    test = test.replace(anchor, addition, 1)
    test_path.write_text(test)

script_path = Path('tests/compiler/c0/run-record-assignment-expressions.sh')
script = script_path.read_text()
old_script = '''"$minic" -S "$work/record_assignment_expression.i" \\
    -o "$work/record_assignment_expression.s"

grep -F 'main:' "$work/record_assignment_expression.s" >/dev/null
'''
new_script = '''"$minic" -S "$work/record_assignment_expression.i" \\
    -o "$work/record_assignment_expression.s"
MINIC_CORE_IR=strict "$minic" -S "$work/record_assignment_expression.i" \\
    -o "$work/record_assignment_expression.strict.s"

grep -F 'main:' "$work/record_assignment_expression.s" >/dev/null
grep -F 'assign_both:' "$work/record_assignment_expression.strict.s" >/dev/null
'''
if 'record_assignment_expression.strict.s' not in script:
    if old_script not in script:
        raise SystemExit('record assignment runner anchor changed')
    script = script.replace(old_script, new_script, 1)
    script = script.replace(
        "printf '%s\\n' 'PASS compiler/c0/record_assignment_expression whole-object-copy=1 comma-discard=1 alias-safe-temp=1'",
        "printf '%s\\n' 'PASS compiler/c0/record_assignment_expression whole-object-copy=1 comma-discard=1 alias-safe-temp=1 chained-record-call-strict=1'",
        1,
    )
    script_path.write_text(script)

print('M127 chained record materialized RHS and permanent regression staged')
