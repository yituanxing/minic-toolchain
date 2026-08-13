#!/usr/bin/env python3
from pathlib import Path

parser_path = Path("src/frontend/parser_global.c")
text = parser_path.read_text()
marker = "static_object_address_relocation_target(program, expression_id, &target->object_id)"
if marker not in text:
    old = """    if (program == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
"""
    new = """    if (program == NULL || target == NULL) {
        return false;
    }
    if (static_object_address_relocation_target(program, expression_id, &target->object_id)) {
        target->member_depth = 0U;
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
"""
    if text.count(old) != 1:
        raise SystemExit("unexpected recursive relocation path anchor")
    text = text.replace(old, new, 1)
    parser_path.write_text(text)

codegen_path = Path("src/target/riscv64/codegen_function.c")
codegen = codegen_path.read_text()
dispatch_marker = "has_recursive_relocation"
if dispatch_marker not in codegen:
    old = """    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete) {
        return false;
    }
    if (!record->is_union && object->initializer_count == record->field_count) {
        return minic_riscv64_emit_direct_record_values(file, program, object, record);
    }
"""
    new = """    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete) {
        return false;
    }
    {
        bool has_recursive_relocation;
        size_t index;

        has_recursive_relocation = false;
        for (index = 0U; index < object->relocation_count; ++index) {
            if (object->relocations[index].location_kind ==
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
                has_recursive_relocation = true;
                break;
            }
        }
        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation) {
            return minic_riscv64_emit_direct_record_values(file, program, object, record);
        }
    }
"""
    if codegen.count(old) != 1:
        raise SystemExit("unexpected record emitter dispatch anchor")
    codegen = codegen.replace(old, new, 1)
    codegen_path.write_text(codegen)

program_path = Path("tests/programs/c0/static_record_compound_literal.c")
program = program_path.read_text()
if "typedef struct NameHolder" not in program:
    old = """typedef struct Outer {
    int tag;
    Inner inner;
} Outer;


static const char *const relocation_names[] = { \"backing\", \"literal\" };
"""
    new = """typedef struct Outer {
    int tag;
    Inner inner;
} Outer;

typedef struct NameHolder {
    const char *name;
} NameHolder;

static const char backing_name[] = \"backing\";
static NameHolder name_holder = { backing_name };

static const char *const relocation_names[] = { \"backing\", \"literal\" };
"""
    if program.count(old) != 1:
        raise SystemExit("unexpected static aggregate fixture anchor")
    program = program.replace(old, new, 1)
    old_main = """                   value.inner.link.prev == &value.inner.link &&
                   relocation_names[0][0] == 'b' && relocation_names[1][0] == 'l'
"""
    new_main = """                   value.inner.link.prev == &value.inner.link && name_holder.name[0] == 'b' &&
                   relocation_names[0][0] == 'b' && relocation_names[1][0] == 'l'
"""
    if program.count(old_main) != 1:
        raise SystemExit("unexpected static aggregate main anchor")
    program = program.replace(old_main, new_main, 1)
    program_path.write_text(program)

constant_path = Path("src/frontend/parser_constant.c")
constant = constant_path.read_text()
octal_marker = "base = 8U; /* C integer constants with a leading zero are octal. */"
if octal_marker not in constant:
    old = """    if (digit_end - span.begin.offset >= 2U && parser->source[offset] == '0' &&
        (parser->source[offset + 1U] == 'x' || parser->source[offset + 1U] == 'X')) {
        base = 16U;
        offset += 2U;
    }
"""
    new = """    if (digit_end - span.begin.offset >= 2U && parser->source[offset] == '0' &&
        (parser->source[offset + 1U] == 'x' || parser->source[offset + 1U] == 'X')) {
        base = 16U;
        offset += 2U;
    } else if (digit_end - span.begin.offset > 1U && parser->source[offset] == '0') {
        base = 8U; /* C integer constants with a leading zero are octal. */
    }
"""
    if constant.count(old) != 2:
        raise SystemExit(f"unexpected integer-radix anchors: {constant.count(old)}")
    constant = constant.replace(old, new)
    constant_path.write_text(constant)

bitwise_path = Path("tests/compiler/c0/integer_constant_bitwise.c")
bitwise = bitwise_path.read_text()
if "octal_permission" not in bitwise:
    bitwise += "\nconst unsigned int octal_permission = 0644;\n"
    bitwise_path.write_text(bitwise)

run_path = Path("tests/compiler/c0/run-integer-constant-bitwise.sh")
run = run_path.read_text()
if "octal_permission" not in run:
    anchor = "grep -F '  .zero 3' \"$work/integer_constant_bitwise.s\" >/dev/null\n"
    replacement = anchor + "sed -n '/^octal_permission:/,/^\\.size/p' \"$work/integer_constant_bitwise.s\" | grep -F '  .word 420' >/dev/null\n"
    if run.count(anchor) != 1:
        raise SystemExit("unexpected integer bitwise gate anchor")
    run = run.replace(anchor, replacement, 1)
    run = run.replace("zero-fill=3'", "zero-fill=3 octal=0644->420'")
    run_path.write_text(run)
