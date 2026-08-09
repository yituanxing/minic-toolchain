#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    '''typedef enum MinicStatementKind {\n    MINIC_STATEMENT_ASSIGN = 0,\n    MINIC_STATEMENT_XOR_ASSIGN,\n''',
    '''typedef enum MinicStatementKind {\n    MINIC_STATEMENT_ASSIGN = 0,\n    MINIC_STATEMENT_RECORD_COPY,\n    MINIC_STATEMENT_XOR_ASSIGN,\n''',
)

# Replace the former field-by-field lowering. It rejected array fields and also modeled
# aggregate assignment as a collection of scalar assignments. Keep one explicit record-copy
# statement so the backend can copy the complete object representation.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
start_marker = "static bool add_record_copy_assignments(MinicParser *parser,\n"
end_marker = "static bool assignment_chain_expression_is_stable("
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("parser_statement.c: record-copy function boundaries not found")
replacement = r'''static bool add_record_copy_assignments(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    MinicStatement statement;

    target = minic_c0_program_expression(parser->program, target_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        source->value_category != MINIC_VALUE_LVALUE || !minic_type_is_record(target->type) ||
        !minic_type_is_record(source->type) || target->type.record_id != source->type.record_id ||
        minic_type_is_const(target->type)) {
        minic_parser_error(parser, "record assignment requires matching modifiable record lvalues");
        return false;
    }
    record = minic_c0_program_record(parser->program, target->type.record_id);
    if (record == NULL || !record->is_complete || record->storage_size == 0U) {
        minic_parser_error(parser, "record assignment requires a complete laid-out record");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RECORD_COPY;
    statement.span = span;
    statement.target_expression = target_id;
    statement.expression = source_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

'''
path.write_text(text[:start] + replacement + text[end:])

# Parser-time records do not have target ABI storage_size yet. Completeness is enough here;
# the backend validates the finalized layout before emitting the copy.
replace_once(
    "src/frontend/parser_statement.c",
    '''    if (record == NULL || !record->is_complete || record->storage_size == 0U) {\n        minic_parser_error(parser, "record assignment requires a complete laid-out record");\n''',
    '''    if (record == NULL || !record->is_complete) {\n        minic_parser_error(parser, "record assignment requires a complete record");\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''    case MINIC_STATEMENT_ASSIGN:\n        return target != NULL && expression != NULL &&\n               target->value_category == MINIC_VALUE_LVALUE &&\n               minic_c0_assignment_compatible(program, target->type, statement->expression);\n    case MINIC_STATEMENT_XOR_ASSIGN: {\n''',
    '''    case MINIC_STATEMENT_ASSIGN:\n        return target != NULL && expression != NULL &&\n               target->value_category == MINIC_VALUE_LVALUE &&\n               minic_c0_assignment_compatible(program, target->type, statement->expression);\n    case MINIC_STATEMENT_RECORD_COPY:\n        return target != NULL && expression != NULL &&\n               target->value_category == MINIC_VALUE_LVALUE &&\n               expression->value_category == MINIC_VALUE_LVALUE &&\n               !minic_type_is_const(target->type) && minic_type_is_record(target->type) &&\n               minic_type_is_record(expression->type) &&\n               target->type.record_id == expression->type.record_id;\n    case MINIC_STATEMENT_XOR_ASSIGN: {\n''',
)

# Add a backend primitive that snapshots the entire source object to temporary stack storage
# before touching the target. Byte copies deliberately avoid alignment assumptions about array
# fields and padding; the layout's complete storage_size is copied exactly.
path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
marker = "static bool minic_riscv64_emit_xor_assignment(FILE *file,\n"
if text.count(marker) != 1:
    raise SystemExit("codegen_statement.c: xor emitter marker mismatch")
emitter = r'''static bool minic_riscv64_emit_record_copy(FILE *file,
                                           const MinicC0Program *program,
                                           const MinicFunction *function,
                                           const MinicStatement *statement) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    size_t storage_size;
    size_t temporary_size;
    size_t index;

    target = minic_c0_program_expression(program, statement->target_expression);
    source = minic_c0_program_expression(program, statement->expression);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        source->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(target->type) ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id) {
        return false;
    }
    record = minic_c0_program_record(program, target->type.record_id);
    if (record == NULL || !record->is_complete || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
    temporary_size = (storage_size + 15U) & ~(size_t)15U;

    if (!minic_riscv64_emit_lvalue_address(file, program, function, statement->expression) ||
        !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
        fprintf(file, "  mv t2, a0\n  mv t3, sp\n") < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    if (!minic_riscv64_emit_lvalue_address(file, program, function, statement->target_expression) ||
        fprintf(file, "  mv t2, sp\n  mv t3, a0\n") < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    return minic_riscv64_emit_stack_release(file, temporary_size);
}

'''
path.write_text(text.replace(marker, emitter + marker, 1))

replace_once(
    "src/target/riscv64/codegen_statement.c",
    '''        case MINIC_STATEMENT_ASSIGN:\n        case MINIC_STATEMENT_XOR_ASSIGN:\n''',
    '''        case MINIC_STATEMENT_ASSIGN:\n        case MINIC_STATEMENT_RECORD_COPY:\n        case MINIC_STATEMENT_XOR_ASSIGN:\n''',
)

replace_once(
    "src/target/riscv64/codegen_statement.c",
    '''    case MINIC_STATEMENT_ASSIGN:\n        return minic_riscv64_emit_assignment(file, program, function, statement);\n\n    case MINIC_STATEMENT_XOR_ASSIGN:\n''',
    '''    case MINIC_STATEMENT_ASSIGN:\n        return minic_riscv64_emit_assignment(file, program, function, statement);\n\n    case MINIC_STATEMENT_RECORD_COPY:\n        return minic_riscv64_emit_record_copy(file, program, function, statement);\n\n    case MINIC_STATEMENT_XOR_ASSIGN:\n''',
)

print("staged whole-record copy including array members")
