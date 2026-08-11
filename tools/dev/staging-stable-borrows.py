from pathlib import Path


def replace_once(path_name: str, old: str, new: str) -> None:
    path = Path(path_name)
    source = path.read_text()
    if old not in source:
        if new in source:
            return
        raise SystemExit(f"patch anchor not found: {path_name}")
    path.write_text(source.replace(old, new, 1))


path = "src/frontend/parser_expression.c"
replace_once(
    path,
    """            MinicExpression comma_expression;
            MinicExpressionId right_id;

            left_expression = minic_c0_program_expression(parser->program, primary_id);
            if (left_expression == NULL || !minic_parser_advance(parser) ||
                !parse_expression_internal(parser, &right_id, 0U, true)) {
                return false;
            }
""",
    """            MinicExpression comma_expression;
            MinicExpressionId right_id;
            MinicSourcePosition left_begin;

            left_expression = minic_c0_program_expression(parser->program, primary_id);
            if (left_expression == NULL) {
                minic_parser_error(parser, "invalid comma expression operand");
                return false;
            }
            left_begin = left_expression->span.begin;
            if (!minic_parser_advance(parser) ||
                !parse_expression_internal(parser, &right_id, 0U, true)) {
                return false;
            }
""",
)
replace_once(
    path,
    "            comma_expression.span.begin = left_expression->span.begin;\n",
    "            comma_expression.span.begin = left_begin;\n",
)
replace_once(
    path,
    """static bool parse_call_arguments(MinicParser *parser,
                                 MinicExpression *call_expression,
                                 const MinicFunction *callee) {
    size_t argument_count;

    argument_count = 0U;
""",
    """static bool parse_call_arguments(MinicParser *parser,
                                 MinicExpression *call_expression,
                                 const MinicFunction *callee) {
    MinicFunction callee_snapshot;
    size_t argument_count;

    if (callee == NULL) {
        return false;
    }
    callee_snapshot = *callee;
    callee = &callee_snapshot;
    argument_count = 0U;
""",
)

path = "src/frontend/parser_postfix.c"
replace_once(
    path,
    """    const MinicExpression *callee;
    const MinicFunctionType *function_type;
    MinicExpression call;
    MinicSourcePosition call_end;

    callee = minic_c0_program_expression(parser->program, callee_id);
    function_type = indirect_callee_type(parser, callee_id);
    if (callee == NULL || function_type == NULL) {
        minic_parser_error(parser, "called expression must have function-pointer type");
        return false;
    }

    (void)memset(&call, 0, sizeof(call));
    call.kind = MINIC_EXPRESSION_CALL;
    call.span.begin = callee->span.begin;
    call.type = function_type->return_type;
    call.value_category = MINIC_VALUE_RVALUE;
    call.value.call.function_id = MINIC_FUNCTION_INVALID;
    call.value.call.callee = callee_id;
    if (!parse_indirect_arguments(parser, &call, function_type)) {
""",
    """    const MinicExpression *callee;
    const MinicFunctionType *function_type;
    MinicFunctionType function_type_snapshot;
    MinicExpression call;
    MinicSourcePosition call_end;

    callee = minic_c0_program_expression(parser->program, callee_id);
    function_type = indirect_callee_type(parser, callee_id);
    if (callee == NULL || function_type == NULL) {
        minic_parser_error(parser, "called expression must have function-pointer type");
        return false;
    }
    function_type_snapshot = *function_type;

    (void)memset(&call, 0, sizeof(call));
    call.kind = MINIC_EXPRESSION_CALL;
    call.span.begin = callee->span.begin;
    call.type = function_type_snapshot.return_type;
    call.value_category = MINIC_VALUE_RVALUE;
    call.value.call.function_id = MINIC_FUNCTION_INVALID;
    call.value.call.callee = callee_id;
    if (!parse_indirect_arguments(parser, &call, &function_type_snapshot)) {
""",
)

path = "src/frontend/parser_statement.c"
replace_once(
    path,
    """    const MinicExpression *target;
    MinicExpression zero;
    MinicExpressionId value_id;
    MinicStatement statement;

    target = minic_c0_program_expression(parser->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "invalid aggregate zero target");
        return false;
    }
""",
    """    const MinicExpression *target;
    MinicExpression zero;
    MinicExpressionId value_id;
    MinicStatement statement;
    MinicType target_type;

    target = minic_c0_program_expression(parser->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        minic_parser_error(parser, "invalid aggregate zero target");
        return false;
    }
    target_type = target->type;
""",
)
replace_once(
    path,
    """    if (!minic_parser_add_expression(parser, &zero, &value_id) ||
        !apply_assignment_conversion(parser, target->type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, target->type, value_id)) {
""",
    """    if (!minic_parser_add_expression(parser, &zero, &value_id) ||
        !apply_assignment_conversion(parser, target_type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, target_type, value_id)) {
""",
)
replace_once(
    path,
    """    MinicExpression address;
    MinicExpressionId address_id;
    size_t field_index;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "aggregate zero initializer requires a record lvalue");
        return false;
    }
    record = minic_c0_program_record(parser->program, base->type.record_id);
""",
    """    MinicExpression address;
    MinicExpressionId address_id;
    MinicRecordId record_id;
    MinicSourceSpan base_span;
    MinicType base_type;
    size_t field_index;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "aggregate zero initializer requires a record lvalue");
        return false;
    }
    base_span = base->span;
    base_type = base->type;
    record_id = base_type.record_id;
    record = minic_c0_program_record(parser->program, record_id);
""",
)
replace_once(path, "    address.span = base->span;\n", "    address.span = base_span;\n")
replace_once(
    path,
    "    if (!minic_type_pointer_to(base->type, &address.type)) {\n",
    "    if (!minic_type_pointer_to(base_type, &address.type)) {\n",
)
replace_once(
    path,
    "        member.value.member.record_id = base->type.record_id;\n",
    "        member.value.member.record_id = record_id;\n",
)
replace_once(
    path,
    """        const MinicExpression *member;
        const MinicExpression *value;
        MinicStatement statement;
""",
    """        const MinicExpression *member;
        const MinicExpression *value;
        MinicSourceSpan member_span;
        MinicStatement statement;
        MinicType member_type;
""",
)
replace_once(
    path,
    """        member = minic_c0_program_expression(parser->program, member_id);
        if (member == NULL || member->value_category != MINIC_VALUE_LVALUE ||
            !apply_assignment_conversion(parser, member->type, &value_id) ||
            !minic_c0_assignment_compatible(parser->program, member->type, value_id)) {
            minic_parser_error(parser, "record designated initializer type mismatch");
            return false;
        }
""",
    """        member = minic_c0_program_expression(parser->program, member_id);
        if (member == NULL || member->value_category != MINIC_VALUE_LVALUE) {
            minic_parser_error(parser, "record designated initializer type mismatch");
            return false;
        }
        member_span = member->span;
        member_type = member->type;
        if (!apply_assignment_conversion(parser, member_type, &value_id) ||
            !minic_c0_assignment_compatible(parser->program, member_type, value_id)) {
            minic_parser_error(parser, "record designated initializer type mismatch");
            return false;
        }
""",
)
replace_once(
    path,
    "        statement.span.begin = member->span.begin;\n",
    "        statement.span.begin = member_span.begin;\n",
)
replace_once(
    path,
    """    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicSourcePosition begin;
""",
    """    MinicExpressionId target_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicSourcePosition begin;
    MinicSourceSpan initializer_span;
    MinicType initializer_type;
""",
)
replace_once(
    path,
    """    local.type = initializer->type;

    /* GNU __auto_type deliberately keeps the new name out of scope while the
""",
    """    initializer_span = initializer->span;
    initializer_type = initializer->type;
    local.type = initializer_type;

    /* GNU __auto_type deliberately keeps the new name out of scope while the
""",
)
replace_once(
    path,
    "            !add_record_copy_assignments(parser, target_id, initializer_id, initializer->span)) {\n",
    "            !add_record_copy_assignments(parser, target_id, initializer_id, initializer_span)) {\n",
)
replace_once(
    path,
    "        statement.span.end = initializer->span.end;\n",
    "        statement.span.end = initializer_span.end;\n",
)

path = "src/frontend/ast.h"
replace_once(
    path,
    "const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,\n",
    """/* Program entity accessors return borrowed pointers into growable owner arrays.
 * IDs remain stable, but growing the same entity array may relocate its storage.
 * Keep an ID or copy required value fields across any operation that may grow that pool. */
const MinicExpression *minic_c0_program_expression(const MinicC0Program *program,
""",
)

path = "tests/compiler/c0/run.sh"
replace_once(
    path,
    'expect_compile_failure invalid_return "expected expression"',
    '''expect_compile_failure invalid_return "expected expression"

MINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-comma-operator.sh"''',
)
