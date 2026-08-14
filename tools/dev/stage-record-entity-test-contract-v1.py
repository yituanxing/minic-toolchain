#!/usr/bin/env python3
from pathlib import Path

path = Path('tests/frontend/record_test.c')
text = path.read_text()
old = '''    if (minic_c0_program_add_record(
            &program,
            "AES_ctx",
            7U,
            &duplicate_id) ||
        minic_c0_program_add_record(&program, "", 0U, &duplicate_id)) {
        minic_c0_program_destroy(&program);
        return fail("invalid tagged record accepted");
    }

'''
new = '''    if (!minic_c0_program_add_record(
            &program,
            "AES_ctx",
            7U,
            &duplicate_id) ||
        duplicate_id == record_id ||
        minic_c0_program_add_record(&program, "", 0U, &duplicate_id)) {
        minic_c0_program_destroy(&program);
        return fail("record entity identity contract");
    }
    record = minic_c0_program_record(&program, duplicate_id);
    if (record == NULL || strcmp(record->name, "AES_ctx") != 0 || record->is_complete ||
        record->field_count != 0U) {
        minic_c0_program_destroy(&program);
        return fail("duplicate-name record entity metadata");
    }

'''
assert text.count(old) == 1
path.write_text(text.replace(old, new, 1))
