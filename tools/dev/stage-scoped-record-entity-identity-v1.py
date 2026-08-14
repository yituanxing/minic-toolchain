#!/usr/bin/env python3
from pathlib import Path

path = Path('src/frontend/ast.c')
text = path.read_text()
old = '''bool minic_c0_program_add_record(MinicC0Program *program,
                                 const char *name,
                                 size_t name_length,
                                 MinicRecordId *record_id) {
    MinicRecord record;
    size_t index;

    if (program == NULL || name == NULL || name_length == 0U || record_id == NULL) {
        return false;
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *existing;

        existing = &program->records[index];
        if (name_length == existing->name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array((void **)&program->records,
'''
new = '''bool minic_c0_program_add_record(MinicC0Program *program,
                                 const char *name,
                                 size_t name_length,
                                 MinicRecordId *record_id) {
    MinicRecord record;

    if (program == NULL || name == NULL || name_length == 0U || record_id == NULL) {
        return false;
    }
    /* RecordId is semantic entity identity. Tag-name uniqueness belongs to the
     * parser's scoped tag namespace, so distinct block scopes may own records
     * with the same diagnostic/display name. */
    if (!minic_grow_array((void **)&program->records,
'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))
