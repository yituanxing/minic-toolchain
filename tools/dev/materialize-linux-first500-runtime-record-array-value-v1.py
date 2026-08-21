#!/usr/bin/env python3
"""Materialize runtime record-array initialization from record values as well as braces."""
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

forward_anchor = '''static bool add_zero_initialized_record_lvalue(MinicParser *parser,
                                               MinicExpressionId base_id,
                                               MinicSourceSpan initializer_span);
'''
forward_replacement = forward_anchor + '''static bool add_record_initializer_copy(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span);
'''
if forward_replacement not in text:
    if text.count(forward_anchor) != 1:
        raise SystemExit("record-copy forward-declaration anchor not found uniquely")
    text = text.replace(forward_anchor, forward_replacement, 1)

helper_anchor = '''static bool add_array_object_element_assignment(MinicParser *parser,
                                                MinicExpressionId base_id,
                                                size_t index,
                                                MinicExpressionId value_id) {
'''
helper = '''static bool parse_runtime_record_array_element_initializer(MinicParser *parser,
                                                           MinicExpressionId target_id,
                                                           MinicType element_type) {
    MinicExpressionId source_id;
    const MinicExpression *source;

    if (parser == NULL || !minic_type_is_record(element_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return minic_parser_parse_runtime_record_initializer(parser, target_id);
    }
    if (!minic_parser_parse_expression(parser, &source_id, 0U)) {
        return false;
    }
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL || !minic_c0_record_value_is_copy_source(parser->program, source_id) ||
        !minic_type_is_record(source->type) || source->type.record_id != element_type.record_id) {
        minic_parser_error(parser,
                           "runtime record array element requires a matching record copy source");
        return false;
    }
    return add_record_initializer_copy(parser, target_id, source_id, source->span);
}

'''
if helper not in text:
    if text.count(helper_anchor) != 1:
        raise SystemExit("record-array helper anchor not found uniquely")
    text = text.replace(helper_anchor, helper + helper_anchor, 1)

old_designated = '''            if (minic_type_is_record(array_info.element_type)) {
                MinicExpressionId element_id;

                if (first != last || parser->current.kind != MINIC_TOKEN_LBRACE) {
                    minic_parser_error(
                        parser, "record array range designators require a single braced element");
                    return false;
                }
                if (!add_array_object_element_lvalue(
                        parser, base_id, first, parser->current.span, &element_id) ||
                    !minic_parser_parse_runtime_record_initializer(parser, element_id)) {
                    return false;
                }
'''
new_designated = '''            if (minic_type_is_record(array_info.element_type)) {
                MinicExpressionId element_id;

                if (first != last) {
                    minic_parser_error(parser, "record array range designators require one element");
                    return false;
                }
                if (!add_array_object_element_lvalue(
                        parser, base_id, first, parser->current.span, &element_id) ||
                    !parse_runtime_record_array_element_initializer(
                        parser, element_id, array_info.element_type)) {
                    return false;
                }
'''
if old_designated in text:
    if text.count(old_designated) != 1:
        raise SystemExit("fixed designated record-array anchor not found uniquely")
    text = text.replace(old_designated, new_designated, 1)

old_fixed = '''            if (minic_type_is_record(array_info.element_type)) {
                MinicExpressionId element_id;

                if (parser->current.kind != MINIC_TOKEN_LBRACE) {
                    minic_parser_error(parser, "runtime record array element requires braces");
                    return false;
                }
                if (!add_array_object_element_lvalue(
                        parser, base_id, initializer_count, parser->current.span, &element_id) ||
                    !minic_parser_parse_runtime_record_initializer(parser, element_id)) {
                    return false;
                }
'''
new_fixed = '''            if (minic_type_is_record(array_info.element_type)) {
                MinicExpressionId element_id;

                if (!add_array_object_element_lvalue(
                        parser, base_id, initializer_count, parser->current.span, &element_id) ||
                    !parse_runtime_record_array_element_initializer(
                        parser, element_id, array_info.element_type)) {
                    return false;
                }
'''
if old_fixed in text:
    if text.count(old_fixed) != 1:
        raise SystemExit("fixed positional record-array anchor not found uniquely")
    text = text.replace(old_fixed, new_fixed, 1)

old_inferred = '''        if (minic_type_is_record(local->type)) {
            MinicExpressionId base_id;
            MinicExpressionId element_id;

            if (parser->current.kind != MINIC_TOKEN_LBRACE) {
                minic_parser_error(parser, "inferred runtime record array element requires braces");
                return false;
            }
            if (!add_local_lvalue_expression(parser, local_id, local->name_span, &base_id) ||
                !add_array_object_element_lvalue(
                    parser, base_id, initializer_count, parser->current.span, &element_id) ||
                !minic_parser_parse_runtime_record_initializer(parser, element_id)) {
                return false;
            }
'''
new_inferred = '''        if (minic_type_is_record(local->type)) {
            MinicExpressionId base_id;
            MinicExpressionId element_id;

            if (!add_local_lvalue_expression(parser, local_id, local->name_span, &base_id) ||
                !add_array_object_element_lvalue(
                    parser, base_id, initializer_count, parser->current.span, &element_id) ||
                !parse_runtime_record_array_element_initializer(parser, element_id, local->type)) {
                return false;
            }
'''
if old_inferred in text:
    if text.count(old_inferred) != 1:
        raise SystemExit("inferred record-array anchor not found uniquely")
    text = text.replace(old_inferred, new_inferred, 1)

path.write_text(text)

case = Path("tests/compiler/c0/runtime_record_array_value_initializer.c")
case.write_text('''struct pair { int left; int right; };\n\nint main(void) {\n    struct pair seed = { 3, 4 };\n    struct pair *ptr = &seed;\n    struct pair values[] = { *ptr, { 5, 6 } };\n    struct pair fixed[2] = { *ptr, { 7, 8 } };\n    return values[0].left == 3 && values[0].right == 4 &&\n                   values[1].left == 5 && values[1].right == 6 &&\n                   fixed[0].left == 3 && fixed[0].right == 4 &&\n                   fixed[1].left == 7 && fixed[1].right == 8\n               ? 0\n               : 1;\n}\n''')

runtime = Path("tests/compiler/c0/run-runtime.sh")
runtime_text = runtime.read_text()
line = "run_case runtime_record_array_value_initializer 0 runtime_record_array_value_initializer\n"
if line not in runtime_text:
    anchor = "run_case static_union_active_member_relocation 0 static_union_active_member_relocation\n"
    if anchor not in runtime_text:
        anchor = "run_case inferred_static_unsigned_char_list 0 inferred_static_unsigned_char_list\n"
    if runtime_text.count(anchor) != 1:
        raise SystemExit("runtime regression anchor not found uniquely")
    runtime_text = runtime_text.replace(anchor, anchor + line, 1)
runtime.write_text(runtime_text)
