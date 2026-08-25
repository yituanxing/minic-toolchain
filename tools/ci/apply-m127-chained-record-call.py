#!/usr/bin/env python3
from pathlib import Path

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
call_pos = text.find(old, pos)
if call_pos < 0:
    raise SystemExit('M109 RHS record-value call changed')
next_marker = text.find('if (!minic_c0_record_value_is_address_backed', pos)
if next_marker < 0 or call_pos > next_marker:
    raise SystemExit('M109 RHS replacement escaped assignment branch')
text = text[:call_pos] + text[call_pos:].replace(old, new, 1)
path.write_text(text)
print('M127 chained record materialized RHS staged')
