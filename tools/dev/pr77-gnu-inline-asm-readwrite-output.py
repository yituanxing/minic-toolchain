#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Grow the first-class InlineAsm record with operand slices. Generic frontend
# metadata records access mode and the raw constraint; target-specific letters
# remain a Target responsibility.
replace_once(
    "src/frontend/ast.h",
    r'''typedef struct MinicInlineAsm {
    char *template_text;
    size_t template_length;
    size_t output_count;
    size_t input_count;
    size_t clobber_count;
    bool is_volatile;
    bool has_memory_clobber;
} MinicInlineAsm;
''',
    r'''typedef enum MinicInlineAsmOutputAccess {
    MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY = 0,
    MINIC_INLINE_ASM_OUTPUT_READ_WRITE
} MinicInlineAsmOutputAccess;

typedef struct MinicInlineAsmOperand {
    char *constraint_text;
    size_t constraint_length;
    MinicExpressionId expression;
    MinicInlineAsmOutputAccess output_access;
} MinicInlineAsmOperand;

typedef struct MinicInlineAsm {
    char *template_text;
    size_t template_length;
    MinicInlineAsmOperand *outputs;
    size_t output_count;
    size_t output_capacity;
    MinicInlineAsmOperand *inputs;
    size_t input_count;
    size_t input_capacity;
    size_t clobber_count;
    bool is_volatile;
    bool has_memory_clobber;
} MinicInlineAsm;
''',
    "inline-asm-operand-record",
)
replace_once(
    "src/frontend/ast.h",
    r'''bool minic_c0_program_add_inline_asm(MinicC0Program *program,
                                     const char *template_text,
                                     size_t template_length,
                                     bool is_volatile,
                                     bool has_memory_clobber,
                                     MinicInlineAsmId *inline_asm_id);
''',
    r'''bool minic_c0_program_add_inline_asm(MinicC0Program *program,
                                     const char *template_text,
                                     size_t template_length,
                                     bool is_volatile,
                                     bool has_memory_clobber,
                                     MinicInlineAsmId *inline_asm_id);
bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,
                                            MinicInlineAsmId inline_asm_id,
                                            const char *constraint_text,
                                            size_t constraint_length,
                                            MinicExpressionId expression,
                                            MinicInlineAsmOutputAccess output_access);
bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,
                                                     MinicInlineAsmId inline_asm_id,
                                                     bool has_memory_clobber);
''',
    "inline-asm-operand-api",
)

# Program ownership includes operand constraint strings and slices.
replace_once(
    "src/frontend/ast.c",
    r'''    for (index = 0U; index < program->inline_asm_count; ++index) {
        free(program->inline_asms[index].template_text);
    }
''',
    r'''    for (index = 0U; index < program->inline_asm_count; ++index) {
        size_t operand_index;

        free(program->inline_asms[index].template_text);
        for (operand_index = 0U; operand_index < program->inline_asms[index].output_count;
             ++operand_index) {
            free(program->inline_asms[index].outputs[operand_index].constraint_text);
        }
        for (operand_index = 0U; operand_index < program->inline_asms[index].input_count;
             ++operand_index) {
            free(program->inline_asms[index].inputs[operand_index].constraint_text);
        }
        free(program->inline_asms[index].outputs);
        free(program->inline_asms[index].inputs);
    }
''',
    "inline-asm-destroy-operands",
)
path = Path("src/frontend/ast.c")
text = path.read_text()
anchor = "bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id) {\n"
helpers = r'''bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,
                                            MinicInlineAsmId inline_asm_id,
                                            const char *constraint_text,
                                            size_t constraint_length,
                                            MinicExpressionId expression,
                                            MinicInlineAsmOutputAccess output_access) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmOperand operand;

    if (program == NULL || inline_asm_id >= program->inline_asm_count ||
        constraint_text == NULL || constraint_length == 0U ||
        expression >= program->expression_count ||
        (output_access != MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY &&
         output_access != MINIC_INLINE_ASM_OUTPUT_READ_WRITE)) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    if (!minic_grow_array((void **)&inline_asm->outputs,
                          &inline_asm->output_capacity,
                          inline_asm->output_count,
                          sizeof(*inline_asm->outputs))) {
        return false;
    }
    (void)memset(&operand, 0, sizeof(operand));
    operand.constraint_text = minic_copy_name(constraint_text, constraint_length);
    if (operand.constraint_text == NULL) {
        return false;
    }
    operand.constraint_length = constraint_length;
    operand.expression = expression;
    operand.output_access = output_access;
    inline_asm->outputs[inline_asm->output_count] = operand;
    inline_asm->output_count += 1U;
    return true;
}

bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,
                                                     MinicInlineAsmId inline_asm_id,
                                                     bool has_memory_clobber) {
    MinicInlineAsm *inline_asm;

    if (program == NULL || inline_asm_id >= program->inline_asm_count) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    inline_asm->has_memory_clobber = has_memory_clobber;
    inline_asm->clobber_count = has_memory_clobber ? 1U : 0U;
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"inline-asm-operand-ast-anchor: expected one add-block anchor, found {text.count(anchor)}")
path.write_text(text.replace(anchor, helpers + anchor, 1))

# Replace the first parser subset with a grammar that supports arbitrary output
# count in the representation, while the active semantic subset accepts generic
# '=' or '+' output access. Inputs remain explicitly unsupported until the next
# real source construct requires them.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
start = text.find("static bool parse_gnu_inline_asm_statement(MinicParser *parser) {")
end = text.find("\nstatic bool token_starts_local_declaration", start)
if start < 0 or end < 0:
    raise SystemExit("inline-asm-output-parser: cannot locate staged asm parser")
replacement = r'''static bool parse_gnu_inline_asm_output(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    const MinicExpression *operand_expression;
    MinicExpressionId operand_id;
    MinicInlineAsmOutputAccess access;
    MinicSourceSpan constraint_span;
    char *constraint;
    size_t constraint_length;

    constraint = NULL;
    constraint_length = 0U;
    if (!minic_parser_parse_string_text(
            parser, &constraint, &constraint_length, &constraint_span)) {
        free(constraint);
        return false;
    }
    if (constraint_length == 0U ||
        (constraint[0] != '+' && constraint[0] != '=')) {
        free(constraint);
        minic_parser_error(parser,
                           "GNU asm output constraint must begin with '+' or '='");
        return false;
    }
    access = constraint[0] == '+' ? MINIC_INLINE_ASM_OUTPUT_READ_WRITE
                                  : MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY;
    if (!minic_parser_expect(parser,
                             MINIC_TOKEN_LPAREN,
                             "expected '(' before GNU asm output expression") ||
        !minic_parser_parse_expression_no_decay(parser, &operand_id) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RPAREN,
                             "expected ')' after GNU asm output expression")) {
        free(constraint);
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand_id);
    if (operand_expression == NULL || operand_expression->value_category != MINIC_VALUE_LVALUE) {
        free(constraint);
        minic_parser_error(parser, "GNU asm output operand requires an lvalue");
        return false;
    }
    if (!minic_c0_program_add_inline_asm_output(parser->program,
                                                inline_asm_id,
                                                constraint,
                                                constraint_length,
                                                operand_id,
                                                access)) {
        free(constraint);
        minic_parser_error(parser, "cannot store GNU asm output operand");
        return false;
    }
    free(constraint);
    return true;
}

static bool parse_gnu_inline_asm_statement(MinicParser *parser) {
    MinicStatement statement;
    MinicInlineAsmId inline_asm_id;
    MinicSourcePosition begin;
    MinicSourceSpan template_span;
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool has_memory_clobber;

    if (!current_is_gnu_asm(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    template_text = NULL;
    template_length = 0U;
    is_volatile = false;
    has_memory_clobber = false;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (current_is_gnu_volatile(parser)) {
        is_volatile = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after GNU asm") ||
        !minic_parser_parse_string_text(
            parser, &template_text, &template_length, &template_span)) {
        free(template_text);
        return false;
    }
    if (strchr(template_text, '%') != NULL) {
        free(template_text);
        minic_parser_error(parser,
                           "GNU asm operand substitutions require target template support");
        return false;
    }
    if (!minic_c0_program_add_inline_asm(parser->program,
                                         template_text,
                                         template_length,
                                         is_volatile,
                                         false,
                                         &inline_asm_id)) {
        free(template_text);
        minic_parser_error(parser, "cannot store GNU inline assembly");
        return false;
    }
    free(template_text);

    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_output(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }

        if (parser->current.kind == MINIC_TOKEN_COLON) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COLON &&
                parser->current.kind != MINIC_TOKEN_RPAREN) {
                minic_parser_error(parser, "GNU asm input operands are not supported yet");
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_COLON) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                while (parser->current.kind != MINIC_TOKEN_RPAREN) {
                    char *clobber;
                    size_t clobber_length;
                    MinicSourceSpan clobber_span;

                    clobber = NULL;
                    clobber_length = 0U;
                    if (!minic_parser_parse_string_text(
                            parser, &clobber, &clobber_length, &clobber_span)) {
                        free(clobber);
                        return false;
                    }
                    if (clobber_length == 6U && memcmp(clobber, "memory", 6U) == 0) {
                        has_memory_clobber = true;
                    } else {
                        free(clobber);
                        minic_parser_error(parser,
                                           "GNU asm register clobbers require TargetConstraint support");
                        return false;
                    }
                    free(clobber);
                    if (parser->current.kind != MINIC_TOKEN_COMMA) {
                        break;
                    }
                    if (!minic_parser_advance(parser)) {
                        return false;
                    }
                }
            }
        }
    }
    if (!minic_c0_program_set_inline_asm_memory_clobber(
            parser->program, inline_asm_id, has_memory_clobber)) {
        minic_parser_error(parser, "cannot finalize GNU inline assembly metadata");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_INLINE_ASM;
    statement.span.begin = begin;
    statement.span.end = parser->current.span.end;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.inline_asm_id = inline_asm_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU asm")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}
'''
path.write_text(text[:start] + replacement + text[end:])

# Inline-asm operand expression IDs are program edges just like statement and
# return expression IDs. Validate all mappings before mutating any operand so a
# failed normalization does not partially rewrite the existing Program.
path = Path("src/frontend/cast_normalization.c")
text = path.read_text()
old = r'''    size_t expression_index;
    size_t statement_index;
    bool success;
'''
new = r'''    size_t expression_index;
    size_t statement_index;
    size_t inline_asm_index;
    bool success;
'''
if text.count(old) != 1:
    raise SystemExit(f"inline-asm-normalization-decls: expected one anchor, found {text.count(old)}")
text = text.replace(old, new, 1)
anchor = r'''    if (success) {
        success =
            remap_program_expression_id(mapping, old_expression_count, &remapped_return_expression);
    }

    if (success) {
        free(program->expressions);
'''
insert = r'''    if (success) {
        success =
            remap_program_expression_id(mapping, old_expression_count, &remapped_return_expression);
    }
    for (inline_asm_index = 0U; success && inline_asm_index < program->inline_asm_count;
         ++inline_asm_index) {
        const MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = &program->inline_asms[inline_asm_index];
        for (operand_index = 0U; success && operand_index < inline_asm->output_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->outputs[operand_index].expression;
            success = old_id < old_expression_count &&
                      mapping != NULL && mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
        for (operand_index = 0U; success && operand_index < inline_asm->input_count;
             ++operand_index) {
            MinicExpressionId old_id;

            old_id = inline_asm->inputs[operand_index].expression;
            success = old_id < old_expression_count &&
                      mapping != NULL && mapping[old_id] != MINIC_EXPRESSION_INVALID;
        }
    }

    if (success) {
        for (inline_asm_index = 0U; inline_asm_index < program->inline_asm_count;
             ++inline_asm_index) {
            MinicInlineAsm *inline_asm;
            size_t operand_index;

            inline_asm = &program->inline_asms[inline_asm_index];
            for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
                inline_asm->outputs[operand_index].expression =
                    mapping[inline_asm->outputs[operand_index].expression];
            }
            for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
                inline_asm->inputs[operand_index].expression =
                    mapping[inline_asm->inputs[operand_index].expression];
            }
        }
        free(program->expressions);
'''
if text.count(anchor) != 1:
    raise SystemExit(f"inline-asm-normalization-loop: expected one anchor, found {text.count(anchor)}")
path.write_text(text.replace(anchor, insert, 1))

# Verifier owns generic data-structure/access invariants. It does not interpret
# RISC-V letters such as r/m; that remains target lowering's responsibility.
path = Path("src/frontend/ast_verifier.c")
text = path.read_text()
start = text.find("    case MINIC_STATEMENT_INLINE_ASM: {")
end = text.find("\n    case MINIC_STATEMENT_RETURN:", start)
if start < 0 or end < 0:
    raise SystemExit("inline-asm-output-verifier: cannot locate staged asm verifier")
replacement = r'''    case MINIC_STATEMENT_INLINE_ASM: {
        const MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
        if (inline_asm == NULL || inline_asm->template_text == NULL ||
            inline_asm->output_count > inline_asm->output_capacity ||
            inline_asm->input_count > inline_asm->input_capacity ||
            (inline_asm->output_count != 0U && inline_asm->outputs == NULL) ||
            (inline_asm->input_count != 0U && inline_asm->inputs == NULL) ||
            inline_asm->input_count != 0U || inline_asm->clobber_count > 1U ||
            inline_asm->clobber_count != (inline_asm->has_memory_clobber ? 1U : 0U) ||
            statement->target_expression != MINIC_EXPRESSION_INVALID ||
            statement->expression != MINIC_EXPRESSION_INVALID ||
            statement->target_statement != MINIC_STATEMENT_INVALID ||
            statement->then_block != MINIC_BLOCK_INVALID ||
            statement->else_block != MINIC_BLOCK_INVALID) {
            return false;
        }
        for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;

            operand = &inline_asm->outputs[operand_index];
            operand_expression = minic_c0_program_expression(program, operand->expression);
            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||
                operand_expression == NULL ||
                operand_expression->value_category != MINIC_VALUE_LVALUE ||
                (operand->output_access != MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY &&
                 operand->output_access != MINIC_INLINE_ASM_OUTPUT_READ_WRITE)) {
                return false;
            }
        }
        return true;
    }
'''
path.write_text(text[:start] + replacement + text[end:])

# RV64 interprets the target constraint. The current real-source capability is
# the kernel's empty-template +rm local barrier: read/write, either register or
# memory. Direct AST codegen has no optimizer, so the physically empty asm emits
# no instruction while the first-class operand edge remains available to Core IR
# and future optimization passes.
path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
start = text.find("    case MINIC_STATEMENT_INLINE_ASM: {")
end = text.find("\n    case MINIC_STATEMENT_RETURN:", start)
if start < 0 or end < 0:
    raise SystemExit("inline-asm-output-codegen: cannot locate staged asm codegen")
replacement = r'''    case MINIC_STATEMENT_INLINE_ASM: {
        const MinicInlineAsm *inline_asm;
        size_t operand_index;

        inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
        if (inline_asm == NULL || inline_asm->template_text == NULL ||
            inline_asm->input_count != 0U) {
            return false;
        }
        for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;

            operand = &inline_asm->outputs[operand_index];
            operand_expression = minic_c0_program_expression(program, operand->expression);
            if (operand_expression == NULL || operand_expression->kind != MINIC_EXPRESSION_LOCAL ||
                operand_expression->value_category != MINIC_VALUE_LVALUE ||
                operand->output_access != MINIC_INLINE_ASM_OUTPUT_READ_WRITE ||
                operand->constraint_length != 3U ||
                memcmp(operand->constraint_text, "+rm", 3U) != 0) {
                return false;
            }
        }
        if (inline_asm->output_count != 0U && inline_asm->template_length != 0U) {
            return false;
        }
        if (inline_asm->template_length == 0U) {
            return true;
        }
        return fprintf(file, "  %s\n", inline_asm->template_text) >= 0;
    }
'''
path.write_text(text[:start] + replacement + text[end:])

print("staged GNU asm read-write output operands with generic access metadata, normalization edges and RV64 +rm validation")
