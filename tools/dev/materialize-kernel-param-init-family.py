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

type_path = Path("src/frontend/type.c")
type_text = type_path.read_text()
qualification_marker = "MinicType unqualified_target_pointee;"
if qualification_marker not in type_text:
    old = """static bool minic_type_pointer_qualification_compatible(MinicType target, MinicType source) {
    MinicType unqualified_target;
    MinicType unqualified_source;

    if (!minic_type_unqualified(target, &unqualified_target) ||
        !minic_type_unqualified(source, &unqualified_source) ||
        unqualified_target.pointer_depth != 1U || unqualified_source.pointer_depth != 1U ||
        !minic_type_same_unqualified_identity(unqualified_target, unqualified_source)) {
        return false;
    }
    return (unqualified_source.base_qualifiers & ~unqualified_target.base_qualifiers) == 0U;
}
"""
    new = """static bool minic_type_pointer_qualification_compatible(MinicType target, MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;
    MinicType unqualified_target_pointee;
    MinicType unqualified_source_pointee;

    if (!minic_type_is_pointer(target) || !minic_type_is_pointer(source) ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) ||
        !minic_type_unqualified(target_pointee, &unqualified_target_pointee) ||
        !minic_type_unqualified(source_pointee, &unqualified_source_pointee) ||
        !minic_type_equal(unqualified_target_pointee, unqualified_source_pointee)) {
        return false;
    }
    if (minic_type_is_const(source_pointee) && !minic_type_is_const(target_pointee)) {
        return false;
    }
    if (minic_type_is_volatile(source_pointee) && !minic_type_is_volatile(target_pointee)) {
        return false;
    }
    return true;
}
"""
    if type_text.count(old) != 1:
        raise SystemExit("unexpected pointer qualification helper anchor")
    type_text = type_text.replace(old, new, 1)
    type_path.write_text(type_text)

qualifier_path = Path("tests/compiler/c0/conditional_pointer_qualifiers.c")
qualifier = qualifier_path.read_text()
if "add_intermediate_const" not in qualifier:
    qualifier += """

const char *const *add_intermediate_const(const char **value) {
    const char *const *result;
    result = value;
    return result;
}
"""
    qualifier_path.write_text(qualifier)

invalid_path = Path("tests/compiler/c0/invalid_deep_pointer_qualification.c")
if not invalid_path.exists():
    invalid_path.write_text("""void reject_deep_qualification(char **source) {
    const char **target;
    target = source;
    (void)target;
}
""")

qualifier_run_path = Path("tests/compiler/c0/run-conditional-pointer-qualifiers.sh")
qualifier_run = qualifier_run_path.read_text()
if "add_intermediate_const" not in qualifier_run:
    qualifier_run = qualifier_run.replace(
        "grep -F 'choose_const_void:' \"$work/conditional_pointer_qualifiers.s\" >/dev/null\n",
        "grep -F 'choose_const_void:' \"$work/conditional_pointer_qualifiers.s\" >/dev/null\n"
        "grep -F 'add_intermediate_const:' \"$work/conditional_pointer_qualifiers.s\" >/dev/null\n"
        "\n"
        "\"$host_cc\" -E -P -x c \"$root/tests/compiler/c0/invalid_deep_pointer_qualification.c\" \\\n"
        "    -o \"$work/invalid_deep_pointer_qualification.i\"\n"
        "if \"$minic\" -S \"$work/invalid_deep_pointer_qualification.i\" \\\n"
        "    -o \"$work/invalid_deep_pointer_qualification.s\" >\"$work/invalid.out\" 2>\"$work/invalid.err\"; then\n"
        "    echo 'expected deep pointer qualification conversion to fail' >&2\n"
        "    exit 1\n"
        "fi\n"
        "grep -F 'assignment expression type does not match target type' \"$work/invalid.err\" >/dev/null\n"
    )
    qualifier_run = qualifier_run.replace(
        "void-object=qualified-void'",
        "void-object=qualified-void immediate-pointee-cv=add deeper-pointee-cv=reject'"
    )
    qualifier_run_path.write_text(qualifier_run)
