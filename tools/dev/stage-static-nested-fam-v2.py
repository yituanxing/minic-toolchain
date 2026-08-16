#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_global.c",
    """    if (field == NULL || field->element_count == 0U) {
        return false;
    }
    for (element_index = 0U; element_index < field->element_count; ++element_index) {
""",
    """    if (field == NULL || field->element_count == 0U) {
        return false;
    }
    /* A flexible array member participates in the record type and alignment,
     * but contributes no scalar initializer slot to the fixed object extent. */
    if (field->is_flexible_array) {
        return true;
    }
    for (element_index = 0U; element_index < field->element_count; ++element_index) {
""",
    "static initializer flexible-array zero fill",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
            return false;
        }
        cursor = 0U;
""",
    """        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        if (record->field_count == 0U) {
            if (type_size != 0U) {
                return false;
            }
            *emitted_size = 0U;
            return true;
        }
        cursor = 0U;
""",
    "recursive emitter empty record",
)

replace_once(
    "src/target/riscv64/codegen_function.c",
    """            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            if (field->is_bit_field) {
""",
    """            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U) {
                return false;
            }
            if (field->is_flexible_array) {
                /* DataLayout gives a trailing FAM zero storage bytes. Its semantic
                 * initializer likewise owns zero scalar slots, so emit nothing. */
                if (record->is_union || field_index + 1U != field_limit) {
                    return false;
                }
                continue;
            }
            if (field->is_bit_field) {
""",
    "recursive emitter flexible-array traversal",
)

Path("tests/compiler/c0/static_nested_flexible_array_initializer.c").write_text(
    r'''struct stats_desc {
    unsigned int flags;
    short exponent;
    unsigned short size;
    unsigned int offset;
    unsigned int bucket_size;
    struct {
        struct {
        } __empty_name;
        char name[];
    };
};

struct wrapped_stats_desc {
    struct stats_desc desc;
    const char *tag;
    char name[8];
};

const struct wrapped_stats_desc nested_fam_rows[] = {
    {
        {
            .flags = 1,
            .exponent = -1,
            .size = 2,
            .offset = 4,
            .bucket_size = 0,
        },
        .tag = "vm",
        .name = "alpha",
    },
    {
        {
            .flags = 3,
            .exponent = 0,
            .size = 1,
            .offset = 8,
            .bucket_size = 16,
        },
        .tag = "vcpu",
        .name = "beta",
    },
};
'''
)

runner = Path("tests/compiler/c0/run-flexible-array-members.sh")
text = runner.read_text()
anchor = "printf '%s\\n' 'PASS compiler/c0/flexible_array_member packed-size=3 payload-offset=3'\n"
insert = anchor + r'''

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_nested_flexible_array_initializer.c" \
    -o "$work/static_nested_flexible_array_initializer.i"
"$minic" -S \
    "$work/static_nested_flexible_array_initializer.i" \
    -o "$work/static_nested_flexible_array_initializer.s"
grep -F '.size nested_fam_rows, 64' \
    "$work/static_nested_flexible_array_initializer.s" >/dev/null
grep -F '.dword .Lminic_string_' \
    "$work/static_nested_flexible_array_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_nested_flexible_array_initializer linux-wrapper zero-slot-fam + relocation'
'''
if text.count(anchor) != 1:
    raise SystemExit(f"flexible-array runner anchor: expected 1 match, found {text.count(anchor)}")
runner.write_text(text.replace(anchor, insert, 1))

print("staged Linux-shaped nested flexible-array zero-slot semantics")
