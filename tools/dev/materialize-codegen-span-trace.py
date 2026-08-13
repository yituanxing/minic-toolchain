#!/usr/bin/env python3
from pathlib import Path
import runpy

runpy.run_path("tools/dev/materialize-inline-asm-matching.py", run_name="__main__")

parser_path = Path("src/frontend/parser_function.c")
text = parser_path.read_text()
old = r'''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType adjusted_type;
    MinicType declared_array_type;
    MinicType pointee_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parser != NULL && parameter_type != NULL;
    }
    if (!minic_parser_parse_array_declarator_suffix(
            parser, *parameter_type, true, &declared_array_type, &is_array) ||
        !is_array || !minic_type_is_array(declared_array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse array parameter declarator");
        }
        return false;
    }
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL) {
        minic_parser_error(parser, "cannot resolve array parameter declarator");
        return false;
    }
    pointee_type = outer_array->element_type;
    if (!minic_type_pointer_to(pointee_type, &adjusted_type) ||
        !minic_c0_program_discard_last_array_type(parser->program, declared_array_type)) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    *parameter_type = adjusted_type;
    return true;
}
'''
new = r'''static bool adjust_array_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    const MinicArrayType *outer_array;
    MinicType adjusted_type;
    MinicType declared_array_type;
    MinicType pointee_type;
    bool discard_declared_array;
    bool is_array;

    if (parser == NULL || parameter_type == NULL) {
        return false;
    }
    discard_declared_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_parse_array_declarator_suffix(
                parser, *parameter_type, true, &declared_array_type, &is_array) ||
            !is_array || !minic_type_is_array(declared_array_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot parse array parameter declarator");
            }
            return false;
        }
        discard_declared_array = true;
    } else if (minic_type_is_array(*parameter_type)) {
        declared_array_type = *parameter_type;
    } else {
        return true;
    }
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL) {
        minic_parser_error(parser, "cannot resolve array parameter declarator");
        return false;
    }
    pointee_type = outer_array->element_type;
    if (!minic_type_pointer_to(pointee_type, &adjusted_type) ||
        (discard_declared_array &&
         !minic_c0_program_discard_last_array_type(parser->program, declared_array_type))) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    *parameter_type = adjusted_type;
    return true;
}
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("typedef-array parameter normalization anchor not found")
old_call = r'''        if (!is_function_pointer_parameter && parser->current.kind == MINIC_TOKEN_LBRACKET &&
            !adjust_array_parameter_type(parser, &parameter_type)) {
            return false;
        }
'''
new_call = r'''        if (!is_function_pointer_parameter &&
            !adjust_array_parameter_type(parser, &parameter_type)) {
            return false;
        }
'''
if old_call in text:
    text = text.replace(old_call, new_call, 1)
elif new_call not in text:
    raise SystemExit("typedef-array parameter normalization call anchor not found")
parser_path.write_text(text)

statement_path = Path("src/target/riscv64/codegen_statement.c")
text = statement_path.read_text()
old = r'''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1);
'''
new = r'''            fprintf(stderr,
                    "CODEGEN_FAIL statement function=%s block=%zu statement=%zu kind=%d line=%zu column=%zu\n",
                    function != NULL ? function->name : "<null>",
                    (size_t)block_id,
                    (size_t)statement_id,
                    statement != NULL ? (int)statement->kind : -1,
                    statement != NULL ? statement->span.begin.line : 0U,
                    statement != NULL ? statement->span.begin.column : 0U);
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("statement codegen trace anchor not found")

old_assignment = r'''    if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_assignment_compatible(program, target->type, statement->expression)) {
        return false;
    }
'''
new_assignment = r'''    if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_assignment_compatible(program, target->type, statement->expression)) {
        fprintf(stderr,
                "CODEGEN_ASSIGN_REJECT target_expr=%zu value_expr=%zu target_kind=%d value_kind=%d target_type=%d/%u target_vcat=%d value_type=%d/%u value_vcat=%d compatible=%d\n",
                (size_t)statement->target_expression,
                (size_t)statement->expression,
                target == NULL ? -1 : (int)target->kind,
                value == NULL ? -1 : (int)value->kind,
                target == NULL ? -1 : (int)target->type.base_kind,
                target == NULL ? 0U : target->type.pointer_depth,
                target == NULL ? -1 : (int)target->value_category,
                value == NULL ? -1 : (int)value->type.base_kind,
                value == NULL ? 0U : value->type.pointer_depth,
                value == NULL ? -1 : (int)value->value_category,
                target != NULL && value != NULL
                    ? (minic_c0_assignment_compatible(program, target->type, statement->expression) ? 1 : 0)
                    : 0);
        return false;
    }
'''
if old_assignment in text:
    text = text.replace(old_assignment, new_assignment, 1)
elif new_assignment not in text:
    raise SystemExit("assignment rejection trace anchor not found")

old_emit = r'''    return minic_riscv64_emit_expression(file, program, function, statement->expression) &&
           fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") >= 0 &&
           minic_riscv64_emit_lvalue_address(
               file, program, function, statement->target_expression) &&
           fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") >= 0 &&
           (!minic_type_is_integer(target->type) ||
            minic_riscv64_emit_integer_conversion(file, target->type, "t0")) &&
           minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0");
'''
new_emit = r'''    if (!minic_riscv64_emit_expression(file, program, function, statement->expression)) {
        fprintf(stderr,
                "CODEGEN_ASSIGN_STAGE value target_expr=%zu value_expr=%zu target_kind=%d value_kind=%d target_type=%d/%u value_type=%d/%u\n",
                (size_t)statement->target_expression,
                (size_t)statement->expression,
                (int)target->kind,
                (int)value->kind,
                (int)target->type.base_kind,
                target->type.pointer_depth,
                (int)value->type.base_kind,
                value->type.pointer_depth);
        return false;
    }
    if (fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0) {
        return false;
    }
    if (!minic_riscv64_emit_lvalue_address(file, program, function, statement->target_expression)) {
        fprintf(stderr,
                "CODEGEN_ASSIGN_STAGE lvalue target_expr=%zu value_expr=%zu target_kind=%d value_kind=%d target_type=%d/%u value_type=%d/%u\n",
                (size_t)statement->target_expression,
                (size_t)statement->expression,
                (int)target->kind,
                (int)value->kind,
                (int)target->type.base_kind,
                target->type.pointer_depth,
                (int)value->type.base_kind,
                value->type.pointer_depth);
        return false;
    }
    if (fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") < 0) {
        return false;
    }
    if (minic_type_is_integer(target->type) &&
        !minic_riscv64_emit_integer_conversion(file, target->type, "t0")) {
        fprintf(stderr, "CODEGEN_ASSIGN_STAGE integer-conversion target_type=%d/%u\n",
                (int)target->type.base_kind, target->type.pointer_depth);
        return false;
    }
    if (!minic_riscv64_emit_scalar_store(file, target->type, "t0", "a0")) {
        fprintf(stderr,
                "CODEGEN_ASSIGN_STAGE store target_expr=%zu value_expr=%zu target_kind=%d value_kind=%d target_type=%d/%u value_type=%d/%u\n",
                (size_t)statement->target_expression,
                (size_t)statement->expression,
                (int)target->kind,
                (int)value->kind,
                (int)target->type.base_kind,
                target->type.pointer_depth,
                (int)value->type.base_kind,
                value->type.pointer_depth);
        return false;
    }
    return true;
'''
if old_emit in text:
    text = text.replace(old_emit, new_emit, 1)
elif new_emit not in text:
    raise SystemExit("assignment emission trace anchor not found")
statement_path.write_text(text)

function_path = Path("src/target/riscv64/codegen_function.c")
text = function_path.read_text()
old = r'''    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_layout(program, function, &frame_layout)) {
        return false;
    }
'''
new = r'''    if (function == NULL || !function->is_defined || function->name_length == 0U ||
        function->body_block >= program->block_count) {
        fprintf(stderr,
                "CODEGEN_FUNCTION_ENTRY invalid-metadata name=%s\n",
                function == NULL ? "<null>" : function->name);
        return false;
    }
    if (!minic_riscv64_frame_layout(program, function, &frame_layout)) {
        size_t parameter_index;

        fprintf(stderr,
                "CODEGEN_FUNCTION_ENTRY frame-layout name=%s params=%zu locals=%zu..%zu\n",
                function->name,
                function->parameter_count,
                (size_t)function->local_begin,
                (size_t)(function->local_begin + function->local_count));
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            MinicLocalId local_id;
            const MinicLocal *parameter;

            local_id = function->local_begin + parameter_index;
            parameter = minic_c0_program_local(program, local_id);
            fprintf(stderr,
                    "CODEGEN_FUNCTION_PARAM index=%zu local=%zu kind=%d ptr=%u array=%d array_id=%zu record=%zu\n",
                    parameter_index,
                    (size_t)local_id,
                    parameter == NULL ? -1 : (int)parameter->type.base_kind,
                    parameter == NULL ? 0U : parameter->type.pointer_depth,
                    parameter != NULL && minic_type_is_array(parameter->type) ? 1 : 0,
                    parameter == NULL ? SIZE_MAX : parameter->type.array_type_id,
                    parameter == NULL ? SIZE_MAX : parameter->type.record_id);
        }
        return false;
    }
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("function entry trace anchor not found")
function_path.write_text(text)

parser_statement_path = Path("src/frontend/parser_statement.c")
text = parser_statement_path.read_text()
old = r'''    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = member_span.begin;
    statement.span.end = value->span.end;
    statement.target_expression = member_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_positional_runtime_record_initializer'''
new = r'''    if (minic_type_is_record(member_type)) {
        if (!minic_type_is_record(value->type) || value->type.record_id != member_type.record_id ||
            !minic_c0_record_value_is_copy_source(parser->program, value_id)) {
            minic_parser_error(parser, "record initializer member requires a matching record copy source");
            return false;
        }
        return add_record_copy_assignments(parser, member_id, value_id, value->span);
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = member_span.begin;
    statement.span.end = value->span.end;
    statement.target_expression = member_id;
    statement.expression = value_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_positional_runtime_record_initializer'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("runtime record member assignment anchor not found")
parser_statement_path.write_text(text)
