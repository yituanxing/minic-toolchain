from pathlib import Path

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

# Global constant payload becomes target-neutral raw bits while preserving the
# legacy int append API for existing producers.
replace_once('src/frontend/ast.h', '    int *initializer_values;\n',
             '    uint64_t *initializer_values;\n', 'initializer payload storage')
replace_once(
    'src/frontend/ast.h',
    '''bool minic_c0_global_object_add_initializer(MinicC0Program *program,\n                                            MinicGlobalObjectId global_object_id,\n                                            int value);\n''',
    '''bool minic_c0_global_object_add_initializer(MinicC0Program *program,\n                                            MinicGlobalObjectId global_object_id,\n                                            int value);\nbool minic_c0_global_object_add_initializer_bits(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id,\n                                                 uint64_t bits);\n''',
    'initializer bits API declaration')

old = '''bool minic_c0_global_object_add_initializer(MinicC0Program *program,\n                                            MinicGlobalObjectId global_object_id,\n                                            int value) {\n    MinicGlobalObject *object;\n\n    if (program == NULL || global_object_id >= program->global_object_count) {\n        return false;\n    }\n    object = &program->global_objects[global_object_id];\n    if (object->is_tentative || object->is_zero_initialized) {\n        return false;\n    }\n    if (object->relocation_count != 0U) {\n        size_t relocation_index;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n        for (relocation_index = 0U; relocation_index < object->relocation_count;\n             ++relocation_index) {\n            const MinicGlobalRelocation *relocation;\n\n            relocation = &object->relocations[relocation_index];\n            if (relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||\n                (relocation->location_index == object->initializer_count && value != 0)) {\n                return false;\n            }\n        }\n    }\n    if (!grow_array((void **)&object->initializer_values,\n                    &object->initializer_capacity,\n                    object->initializer_count,\n                    sizeof(*object->initializer_values))) {\n        return false;\n    }\n    object->initializer_values[object->initializer_count] = value;\n    object->initializer_count += 1U;\n    return true;\n}\n'''
new = '''bool minic_c0_global_object_add_initializer_bits(MinicC0Program *program,\n                                                 MinicGlobalObjectId global_object_id,\n                                                 uint64_t bits) {\n    MinicGlobalObject *object;\n\n    if (program == NULL || global_object_id >= program->global_object_count) {\n        return false;\n    }\n    object = &program->global_objects[global_object_id];\n    if (object->is_tentative || object->is_zero_initialized) {\n        return false;\n    }\n    if (object->relocation_count != 0U) {\n        size_t relocation_index;\n\n        if (!minic_type_is_record(object->type)) {\n            return false;\n        }\n        for (relocation_index = 0U; relocation_index < object->relocation_count;\n             ++relocation_index) {\n            const MinicGlobalRelocation *relocation;\n\n            relocation = &object->relocations[relocation_index];\n            if (relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD ||\n                (relocation->location_index == object->initializer_count && bits != 0U)) {\n                return false;\n            }\n        }\n    }\n    if (!grow_array((void **)&object->initializer_values,\n                    &object->initializer_capacity,\n                    object->initializer_count,\n                    sizeof(*object->initializer_values))) {\n        return false;\n    }\n    object->initializer_values[object->initializer_count] = bits;\n    object->initializer_count += 1U;\n    return true;\n}\n\nbool minic_c0_global_object_add_initializer(MinicC0Program *program,\n                                            MinicGlobalObjectId global_object_id,\n                                            int value) {\n    return minic_c0_global_object_add_initializer_bits(\n        program, global_object_id, (uint64_t)(int64_t)value);\n}\n'''
replace_once('src/frontend/ast_global.c', old, new, 'initializer raw-bits implementation')

# Typed integer initializer bits are the canonical parser boundary; the int API
# remains only for consumers that have not migrated yet.
replace_once(
    'src/frontend/parser_internal.h',
    '''bool minic_parser_parse_integer_initializer_value(MinicParser *parser,\n                                                  MinicType target_type,\n                                                  int *value);\n''',
    '''bool minic_parser_parse_integer_initializer_bits(MinicParser *parser,\n                                                 MinicType target_type,\n                                                 uint64_t *bits);\nbool minic_parser_parse_integer_initializer_value(MinicParser *parser,\n                                                  MinicType target_type,\n                                                  int *value);\n''',
    'typed initializer bits declaration')

core = root / 'src/frontend/parser_core.c'
text = core.read_text()
start = text.index('bool minic_parser_parse_integer_initializer_value(')
end = text.index('\nbool minic_parser_parse_fixed_array_bound', start)
old_block = text[start:end]
new_block = r'''bool minic_parser_parse_integer_initializer_bits(MinicParser *parser,
                                                 MinicType target_type,
                                                 uint64_t *bits) {
    MinicConstValue constant;
    MinicConstValue converted;
    MinicExpressionId expression_id;

    if (parser == NULL || bits == NULL || !minic_type_is_integer(target_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "integer initializer requires an integer target type");
        }
        return false;
    }
    if (!minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "integer initializer type mismatch");
        return false;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant) ||
        !minic_const_value_convert_integer(
            parser->program, parser->target_info, &constant, target_type, &converted)) {
        minic_parser_error(parser, "integer initializer requires a convertible constant expression");
        return false;
    }
    *bits = converted.bits;
    return true;
}

bool minic_parser_parse_integer_initializer_value(MinicParser *parser,
                                                  MinicType target_type,
                                                  int *value) {
    uint64_t bits;
    int64_t signed_value;
    MinicConstValue converted;
    unsigned int width;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_integer_initializer_bits(parser, target_type, &bits) ||
        !minic_target_info_integer_width(
            parser->target_info, parser->program, target_type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    converted.type = target_type;
    converted.bits = bits;
    if (width <= 32U && minic_type_is_unsigned_integer(target_type)) {
        uint32_t raw;

        _Static_assert(sizeof(int) == sizeof(uint32_t),
                       "MiniC legacy initializer payload requires 32-bit host int");
        raw = (uint32_t)bits;
        if (width < 32U) {
            raw &= (UINT32_C(1) << width) - UINT32_C(1);
            *value = (int)raw;
        } else {
            (void)memcpy(value, &raw, sizeof(raw));
        }
        return true;
    }
    if (!minic_const_value_as_int64(
            parser->program, parser->target_info, &converted, &signed_value) ||
        signed_value < INT_MIN || signed_value > INT_MAX) {
        minic_parser_error(parser, "integer initializer exceeds legacy int payload range");
        return false;
    }
    *value = (int)signed_value;
    return true;
}
'''
core.write_text(text[:start] + new_block + text[end:])

# Static pointer bit-pattern constants: require an explicit pointer cast whose
# operand is an integer constant expression. Plain nonzero integers remain closed.
global_c = root / 'src/frontend/parser_global.c'
text = global_c.read_text()
anchor = '''static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {\n'''
helper = r'''static bool static_pointer_integer_constant_bits(const MinicParser *parser,
                                                 MinicExpressionId expression_id,
                                                 uint64_t *bits) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicConstValue constant;
    int64_t signed_value;
    const MinicDataLayout *layout;
    unsigned int pointer_bits;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_CAST ||
        !minic_type_is_pointer(expression->type)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    if (operand == NULL || !minic_type_is_integer(operand->type) ||
        !minic_const_eval_integer(parser->program,
                                  parser->target_info,
                                  expression->value.unary.operand,
                                  &constant) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &signed_value)) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (layout == NULL || layout->pointer_size == 0U || layout->pointer_size > 8U) {
        return false;
    }
    pointer_bits = (unsigned int)(layout->pointer_size * 8U);
    *bits = (uint64_t)signed_value;
    if (pointer_bits < 64U) {
        *bits &= (UINT64_C(1) << pointer_bits) - UINT64_C(1);
    }
    return true;
}

static bool parse_static_pointer_constant_bits(MinicParser *parser,
                                               MinicType target_type,
                                               uint64_t *bits) {
    MinicExpressionId expression_id;

    if (parser == NULL || bits == NULL || !minic_type_is_pointer(target_type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
        *bits = 0U;
        return true;
    }
    if (!static_pointer_integer_constant_bits(parser, expression_id, bits)) {
        minic_parser_error(parser,
                           "static pointer constant requires null or explicit integer-to-pointer cast");
        return false;
    }
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f'pointer bits helper anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helper + anchor, 1)
# Top-level static scalar: accept pointer bits after symbolic address attempts.
old = '''            } else {\n                minic_parser_error(parser,\n                                   "static pointer initializer requires a null or zero-addend "\n                                   "object address constant");\n                return false;\n            }\n'''
new = '''            } else {\n                uint64_t pointer_bits;\n\n                if (!static_pointer_integer_constant_bits(parser, initializer_id, &pointer_bits) ||\n                    !minic_c0_global_object_add_initializer_bits(\n                        parser->program, object_id, pointer_bits)) {\n                    minic_parser_error(parser,\n                                       "static pointer initializer requires null, symbolic address, "\n                                       "or explicit integer-to-pointer constant cast");\n                    return false;\n                }\n            }\n'''
if text.count(old) < 1:
    raise SystemExit('top-level pointer fallback anchor missing')
text = text.replace(old, new, 1)
# Nested scalar integer and pointer leaves now append raw bits.
old = '''    if (minic_type_is_integer(type)) {\n        int parsed;\n\n        if (!minic_parser_parse_integer_initializer_value(parser, type, &parsed) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, parsed)) {\n'''
new = '''    if (minic_type_is_integer(type)) {\n        uint64_t parsed_bits;\n\n        if (!minic_parser_parse_integer_initializer_bits(parser, type, &parsed_bits) ||\n            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, parsed_bits)) {\n'''
if text.count(old) != 1:
    raise SystemExit(f'nested integer bits anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
old = '''    } else if (minic_type_is_pointer(type)) {\n        if (!minic_parser_parse_zero_pointer_constant(parser) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot record static null-pointer initializer");\n            }\n            return false;\n        }\n'''
new = '''    } else if (minic_type_is_pointer(type)) {\n        uint64_t pointer_bits;\n\n        if (!parse_static_pointer_constant_bits(parser, type, &pointer_bits) ||\n            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, pointer_bits)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot record static pointer constant bits");\n            }\n            return false;\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f'nested pointer bits anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
global_c.write_text(text)

# RV64 emits raw initializer bits according to semantic field width. Keep the
# historical signed-decimal spelling for widths >= 2 while preserving bits.
codegen = root / 'src/target/riscv64/codegen_function.c'
text = codegen.read_text()
text = text.replace('#include <errno.h>\n', '#include <errno.h>\n#include <inttypes.h>\n', 1)
anchor = '''static const char *minic_riscv64_integer_data_directive(size_t width) {\n    return width == 1U   ? ".byte"\n           : width == 2U ? ".half"\n           : width == 4U ? ".word"\n           : width == 8U ? ".dword"\n                         : NULL;\n}\n'''
helper = anchor + r'''
static bool minic_riscv64_emit_integer_bits(FILE *file, size_t width, uint64_t bits) {
    const char *directive;
    uint64_t mask;
    int64_t signed_value;

    directive = minic_riscv64_integer_data_directive(width);
    if (file == NULL || directive == NULL) {
        return false;
    }
    if (width == 1U) {
        return fprintf(file, "  %s %u\n", directive, (unsigned int)(bits & UINT64_C(0xff))) >= 0;
    }
    if (width < 8U) {
        unsigned int bit_width;
        uint64_t sign_bit;

        bit_width = (unsigned int)(width * 8U);
        mask = (UINT64_C(1) << bit_width) - UINT64_C(1);
        bits &= mask;
        sign_bit = UINT64_C(1) << (bit_width - 1U);
        if ((bits & sign_bit) != 0U) {
            bits |= ~mask;
        }
    }
    (void)memcpy(&signed_value, &bits, sizeof(signed_value));
    return fprintf(file, "  %s %" PRId64 "\n", directive, signed_value) >= 0;
}
'''
if text.count(anchor) != 1:
    raise SystemExit(f'codegen bits helper anchor mismatch: {text.count(anchor)}')
text = text.replace(anchor, helper, 1)
# Direct record values.
text = text.replace('        int value;\n', '        uint64_t value;\n', 1)
old = '''        } else if (minic_type_is_integer(field->type)) {\n            const char *directive;\n\n            directive = minic_riscv64_integer_data_directive(field_size);\n            if (directive == NULL) {\n                return false;\n            }\n            if (field_size == 1U) {\n                unsigned int byte_value;\n\n                byte_value = (unsigned int)value & 0xffU;\n                if (fprintf(file, "  %s %u\\n", directive, byte_value) < 0) {\n                    return false;\n                }\n            } else if (fprintf(file, "  %s %d\\n", directive, value) < 0) {\n                return false;\n            }\n        } else {\n            if (value != 0 ||\n                (!minic_type_is_record(field->type) && !minic_type_is_pointer(field->type)) ||\n                !minic_riscv64_emit_zero_bytes(file, field_size)) {\n                return false;\n            }\n        }\n'''
new = '''        } else if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {\n            if (!minic_riscv64_emit_integer_bits(file, field_size, value)) {\n                return false;\n            }\n        } else {\n            if (value != 0U || !minic_type_is_record(field->type) ||\n                !minic_riscv64_emit_zero_bytes(file, field_size)) {\n                return false;\n            }\n        }\n'''
if text.count(old) != 1:
    raise SystemExit(f'direct record emitter anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
# Generic recursive integer and pointer leaf emission.
old = '''    if (minic_type_is_integer(type)) {\n        const char *directive;\n        int value;\n\n        if (*initializer_index >= object->initializer_count) {\n            return false;\n        }\n        value = object->initializer_values[*initializer_index];\n        *initializer_index += 1U;\n        directive = minic_riscv64_integer_data_directive(type_size);\n        if (directive == NULL) {\n            return false;\n        }\n        if (type_size == 1U) {\n            unsigned int byte_value;\n\n            byte_value = (unsigned int)value & 0xffU;\n            if (fprintf(file, "  %s %u\\n", directive, byte_value) < 0) {\n                return false;\n            }\n        } else if (fprintf(file, "  %s %d\\n", directive, value) < 0) {\n            return false;\n        }\n        *emitted_size = type_size;\n        return true;\n    }\n    if (minic_type_is_pointer(type)) {\n        int value;\n\n        if (*initializer_index >= object->initializer_count) {\n            return false;\n        }\n        value = object->initializer_values[*initializer_index];\n        *initializer_index += 1U;\n        if (value != 0 || !minic_riscv64_emit_zero_bytes(file, type_size)) {\n            return false;\n        }\n        *emitted_size = type_size;\n        return true;\n    }\n'''
new = '''    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {\n        uint64_t bits;\n\n        if (*initializer_index >= object->initializer_count) {\n            return false;\n        }\n        bits = object->initializer_values[*initializer_index];\n        *initializer_index += 1U;\n        if (!minic_riscv64_emit_integer_bits(file, type_size, bits)) {\n            return false;\n        }\n        *emitted_size = type_size;\n        return true;\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f'generic scalar emitter anchor mismatch: {text.count(old)}')
text = text.replace(old, new, 1)
codegen.write_text(text)

# Focused Linux-shaped pointer sentinel in the already established nested compound literal.
test = root / 'tests/compiler/c0/static_record_compound_literal.c'
text = test.read_text()
text = text.replace('    int second;\n', '    int second;\n    void *owner;\n', 1)
text = text.replace('.magic = 0xdead4ead, .second = 7 }',
                    '.magic = 0xdead4ead, .second = 7, .owner = (void *)-1L }', 1)
text = text.replace('value.inner.second == 7\n',
                    'value.inner.second == 7 && value.inner.owner == (void *)-1L\n', 1)
test.write_text(text)

script = root / 'tests/compiler/c0/run-static-aggregate-family-discovery.sh'
text = script.read_text()
needle = "grep -F '.word -559067475' \"$build_dir/static_record_compound_literal.s\" >/dev/null\n"
if text.count(needle) != 1:
    raise SystemExit(f'focused pointer sentinel anchor mismatch: {text.count(needle)}')
text = text.replace(needle, needle + "grep -F '.dword -1' \"$build_dir/static_record_compound_literal.s\" >/dev/null\n", 1)
script.write_text(text)
