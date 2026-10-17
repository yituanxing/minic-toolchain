#!/usr/bin/env python3
"""One-factor, disposable RV64 layout validation simplification.

The complete record type query already visits and validates all fields in
minic_data_layout_record_depth. A later request of each field in turn repeats
that exact same full walk, but neither reads nor stores the resulting offsets.
Keep the whole-record validation and remove only that redundant second loop.
No DataLayout ABI, caching, record mutation, or diagnosed error path changes.
"""
from pathlib import Path

p=Path("src/target/riscv64/layout.c")
s=p.read_text()
start=s.index("static bool minic_riscv64_layout_records(")
end=s.index("\nstatic bool minic_riscv64_layout_globals(",start)
body=s[start:end]
old='''        size_t field_index;
        size_t storage_size;
        size_t alignment;'''
new='''        size_t storage_size;
        size_t alignment;'''
if body.count(old)!=1:raise SystemExit("layout-local-declarations changed")
body=body.replace(old,new,1)
old='''        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            size_t field_offset;
            size_t bit_offset;

            if (!minic_data_layout_record_field_layout(
                    layout, program, record, field_index, &field_offset, &bit_offset)) {
                return false;
            }
        }'''
new='''        /* Full-record DataLayout has already checked every field for
         * validity while deriving storage_size and alignment. Repeating
         * the full recursive walk for EACH field here was quadratic.
         * The retrieved offsets were never used. */'''
if body.count(old)!=1:raise SystemExit("redundant field validation changed")
body=body.replace(old,new,1)
p.write_text(s[:start]+body+s[end:])
print("MINIC_PERF_RECORD_LAYOUT_SINGLE_WALK=APPLIED rv64_only=true")
