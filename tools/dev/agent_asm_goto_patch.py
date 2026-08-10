#!/usr/bin/env python3
from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# Inline-asm labels are a small extension of the existing semantic object: they
# preserve source names only until the parser resolves them to the same
# MinicStatementId identity already used by ordinary goto/labels.
replace_once(
    "src/frontend/ast.h",
    "typedef struct MinicInlineAsm {\n",
    "typedef struct MinicInlineAsmLabel {\n"
    "    char *name;\n"
    "    size_t name_length;\n"
    "    MinicStatementId target_statement;\n"
    "} MinicInlineAsmLabel;\n\n"
    "typedef struct MinicInlineAsm {\n",
    "asm-goto-label-struct",
)
replace_once(
    "src/frontend/ast.h",
    "    MinicInlineAsmOperand *inputs;\n"
    "    size_t input_count;\n"
    "    size_t input_capacity;\n"
    "    size_t clobber_count;\n"
    "    bool is_volatile;\n"
    "    bool has_memory_clobber;\n",
    "    MinicInlineAsmOperand *inputs;\n"
    "    size_t input_count;\n"
    "    size_t input_capacity;\n"
    "    MinicInlineAsmLabel *labels;\n"
    "    size_t label_count;\n"
    "    size_t label_capacity;\n"
    "    size_t clobber_count;\n"
    "    bool is_volatile;\n"
    "    bool is_goto;\n"
    "    bool has_memory_clobber;\n",
    "asm-goto-storage",
)
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,\n"
    "                                                    MinicInlineAsmId inline_asm_id,\n"
    "                                                    bool has_memory_clobber);\n",
    "bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,\n"
    "                                                    MinicInlineAsmId inline_asm_id,\n"
    "                                                    bool has_memory_clobber);\n"
    "bool minic_c0_program_set_inline_asm_goto(MinicC0Program *program,\n"
    "                                          MinicInlineAsmId inline_asm_id,\n"
    "                                          bool is_goto);\n"
    "bool minic_c0_program_add_inline_asm_label(MinicC0Program *program,\n"
    "                                           MinicInlineAsmId inline_asm_id,\n"
    "                                           const char *name,\n"
    "                                           size_t name_length,\n"
    "                                           MinicStatementId target_statement);\n",
    "asm-goto-api",
)

# AST lifetime and mutators.
replace_once(
    "src/frontend/ast.c",
    "        free(program->inline_asms[index].outputs);\n"
    "        free(program->inline_asms[index].inputs);\n",
    "        for (operand_index = 0U; operand_index < program->inline_asms[index].label_count;\n"
    "             ++operand_index) {\n"
    "            free(program->inline_asms[index].labels[operand_index].name);\n"
    "        }\n"
    "        free(program->inline_asms[index].outputs);\n"
    "        free(program->inline_asms[index].inputs);\n"
    "        free(program->inline_asms[index].labels);\n",
    "asm-goto-destroy",
)
replace_once(
    "src/frontend/ast.c",
    "bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id) {\n",
    r'''bool minic_c0_program_set_inline_asm_goto(MinicC0Program *program,
                                          MinicInlineAsmId inline_asm_id,
                                          bool is_goto) {
    if (program == NULL || inline_asm_id >= program->inline_asm_count) {
        return false;
    }
    program->inline_asms[inline_asm_id].is_goto = is_goto;
    return true;
}

bool minic_c0_program_add_inline_asm_label(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
                                           const char *name,
                                           size_t name_length,
                                           MinicStatementId target_statement) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmLabel label;
    size_t index;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || name == NULL ||
        name_length == 0U ||
        (target_statement != MINIC_STATEMENT_INVALID &&
         target_statement >= program->statement_count)) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    for (index = 0U; index < inline_asm->label_count; ++index) {
        if (inline_asm->labels[index].name_length == name_length &&
            memcmp(inline_asm->labels[index].name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array((void **)&inline_asm->labels,
                          &inline_asm->label_capacity,
                          inline_asm->label_count,
                          sizeof(*inline_asm->labels))) {
        return false;
    }
    (void)memset(&label, 0, sizeof(label));
    label.name = minic_copy_name(name, name_length);
    if (label.name == NULL) {
        return false;
    }
    label.name_length = name_length;
    label.target_statement = target_statement;
    inline_asm->labels[inline_asm->label_count] = label;
    inline_asm->label_count += 1U;
    return true;
}

bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id) {
''',
    "asm-goto-mutators",
)

# Parser helpers: optional `goto` qualifier, fourth-colon label list, and
# forward-resolution when the ordinary label definition is later materialized.
replace_once(
    "src/frontend/parser_statement.c",
    "static bool current_is_gnu_volatile(const MinicParser *parser) {\n",
    r'''static bool current_is_gnu_goto(const MinicParser *parser) {
    return inline_asm_identifier_is(parser, "goto");
}

static bool current_is_gnu_volatile(const MinicParser *parser) {
''',
    "asm-goto-qualifier-helper",
)
replace_once(
    "src/frontend/parser_statement.c",
    "static bool parse_gnu_inline_asm_statement(MinicParser *parser) {\n",
    r'''static bool parse_gnu_inline_asm_label(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    MinicSourceSpan name_span;
    MinicStatementId target_statement;
    const char *name;
    size_t name_length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected GNU asm goto label name");
        }
        return false;
    }
    name_span = parser->current.span;
    name = parser->source + name_span.begin.offset;
    name_length = minic_parser_span_length(name_span);
    target_statement = minic_parser_find_label_statement(parser, name_span);
    return minic_c0_program_add_inline_asm_label(parser->program,
                                                 inline_asm_id,
                                                 name,
                                                 name_length,
                                                 target_statement) &&
           minic_parser_advance(parser);
}

static bool parse_gnu_inline_asm_statement(MinicParser *parser) {
''',
    "asm-goto-label-parser",
)

# Replace the inline-asm parser as a bounded unit. Existing output/input/clobber
# helpers remain unchanged; only qualifier/section sequencing is generalized.
p = Path("src/frontend/parser_statement.c")
text = p.read_text()
start = text.index("static bool parse_gnu_inline_asm_statement(MinicParser *parser) {")
end = text.index("\nstatic bool token_starts_local_declaration", start)
new_parser = r'''static bool parse_gnu_inline_asm_statement(MinicParser *parser) {
    MinicStatement statement;
    MinicInlineAsmId inline_asm_id;
    MinicSourcePosition begin;
    MinicSourceSpan template_span;
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool is_goto;
    bool has_memory_clobber;

    if (!current_is_gnu_asm(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    template_text = NULL;
    template_length = 0U;
    is_volatile = false;
    is_goto = false;
    has_memory_clobber = false;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        if (current_is_gnu_volatile(parser)) {
            if (is_volatile) {
                minic_parser_error(parser, "duplicate volatile qualifier on GNU asm");
                return false;
            }
            is_volatile = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (current_is_gnu_goto(parser)) {
            if (is_goto) {
                minic_parser_error(parser, "duplicate goto qualifier on GNU asm");
                return false;
            }
            is_goto = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        break;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after GNU asm") ||
        !minic_parser_parse_string_text(parser, &template_text, &template_length, &template_span)) {
        free(template_text);
        return false;
    }
    if (!minic_c0_program_add_inline_asm(
            parser->program, template_text, template_length, is_volatile, false, &inline_asm_id) ||
        !minic_c0_program_set_inline_asm_goto(parser->program, inline_asm_id, is_goto)) {
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
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_input(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
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
                minic_parser_error(
                    parser, "GNU asm register clobbers require TargetConstraint support");
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
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!is_goto) {
            minic_parser_error(parser, "GNU asm label operands require asm goto");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_label(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (is_goto) {
        const MinicInlineAsm *inline_asm;

        inline_asm = minic_c0_program_inline_asm(parser->program, inline_asm_id);
        if (inline_asm == NULL || inline_asm->output_count != 0U || inline_asm->label_count == 0U) {
            minic_parser_error(parser,
                               "GNU asm goto currently requires no outputs and at least one label");
            return false;
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
p.write_text(text[:start] + new_parser + text[end:])

# Forward ordinary labels resolve pending asm-goto label operands in the same
# function statement range, just like the existing pending goto repair.
replace_once(
    "src/frontend/parser_statement.c",
    "static bool parse_label(MinicParser *parser, bool allow_declaration) {\n",
    r'''static void resolve_pending_inline_asm_labels(MinicParser *parser,
                                              MinicSourceSpan name_span,
                                              MinicStatementId label_statement_id) {
    size_t statement_index;
    size_t name_length;
    const char *name;

    if (parser == NULL || label_statement_id == MINIC_STATEMENT_INVALID) {
        return;
    }
    name = parser->source + name_span.begin.offset;
    name_length = minic_parser_span_length(name_span);
    for (statement_index = parser->function_statement_begin;
         statement_index < label_statement_id;
         ++statement_index) {
        const MinicStatement *statement;
        MinicInlineAsm *inline_asm;
        size_t label_index;

        statement = minic_c0_program_statement(parser->program, statement_index);
        if (statement == NULL || statement->kind != MINIC_STATEMENT_INLINE_ASM ||
            statement->inline_asm_id >= parser->program->inline_asm_count) {
            continue;
        }
        inline_asm = &parser->program->inline_asms[statement->inline_asm_id];
        for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
            MinicInlineAsmLabel *label;

            label = &inline_asm->labels[label_index];
            if (label->target_statement == MINIC_STATEMENT_INVALID &&
                label->name_length == name_length && memcmp(label->name, name, name_length) == 0) {
                label->target_statement = label_statement_id;
            }
        }
    }
}

static bool parse_label(MinicParser *parser, bool allow_declaration) {
''',
    "asm-goto-forward-resolver",
)
replace_once(
    "src/frontend/parser_statement.c",
    "    if (!is_local_label) {\n"
    "        for (statement_index = parser->function_statement_begin;\n"
    "             statement_index < label_statement_id;\n"
    "             ++statement_index) {\n"
    "            MinicStatement *pending;\n\n"
    "            pending = &parser->program->statements[statement_index];\n"
    "            if (pending->kind == MINIC_STATEMENT_GOTO &&\n"
    "                pending->target_statement == MINIC_STATEMENT_INVALID &&\n"
    "                minic_parser_span_equals(parser, pending->span, name_span)) {\n"
    "                pending->target_statement = label_statement_id;\n"
    "            }\n"
    "        }\n"
    "    }\n",
    "    if (!is_local_label) {\n"
    "        for (statement_index = parser->function_statement_begin;\n"
    "             statement_index < label_statement_id;\n"
    "             ++statement_index) {\n"
    "            MinicStatement *pending;\n\n"
    "            pending = &parser->program->statements[statement_index];\n"
    "            if (pending->kind == MINIC_STATEMENT_GOTO &&\n"
    "                pending->target_statement == MINIC_STATEMENT_INVALID &&\n"
    "                minic_parser_span_equals(parser, pending->span, name_span)) {\n"
    "                pending->target_statement = label_statement_id;\n"
    "            }\n"
    "        }\n"
    "        resolve_pending_inline_asm_labels(parser, name_span, label_statement_id);\n"
    "    }\n",
    "asm-goto-forward-resolve-call",
)

# AST verifier owns the invariant that all asm-goto label names have resolved to
# real LABEL statements before target lowering.
replace_once(
    "src/frontend/ast_verifier.c",
    "        if (inline_asm == NULL || inline_asm->template_text == NULL ||\n"
    "            inline_asm->output_count > inline_asm->output_capacity ||\n"
    "            inline_asm->input_count > inline_asm->input_capacity ||\n",
    "        if (inline_asm == NULL || inline_asm->template_text == NULL ||\n"
    "            inline_asm->output_count > inline_asm->output_capacity ||\n"
    "            inline_asm->input_count > inline_asm->input_capacity ||\n"
    "            inline_asm->label_count > inline_asm->label_capacity ||\n",
    "asm-goto-verifier-capacity",
)
replace_once(
    "src/frontend/ast_verifier.c",
    "            (inline_asm->input_count != 0U && inline_asm->inputs == NULL) ||\n"
    "            inline_asm->clobber_count > 1U ||\n",
    "            (inline_asm->input_count != 0U && inline_asm->inputs == NULL) ||\n"
    "            (inline_asm->label_count != 0U && inline_asm->labels == NULL) ||\n"
    "            inline_asm->clobber_count > 1U ||\n"
    "            (inline_asm->is_goto ? (inline_asm->label_count == 0U || inline_asm->output_count != 0U)\n"
    "                                 : inline_asm->label_count != 0U) ||\n",
    "asm-goto-verifier-shape",
)
replace_once(
    "src/frontend/ast_verifier.c",
    "        return true;\n    }\n\n    case MINIC_STATEMENT_RETURN:\n",
    r'''        for (operand_index = 0U; operand_index < inline_asm->label_count; ++operand_index) {
            const MinicInlineAsmLabel *label;
            const MinicStatement *target_statement;

            label = &inline_asm->labels[operand_index];
            target_statement = minic_c0_program_statement(program, label->target_statement);
            if (label->name == NULL || label->name_length == 0U || target_statement == NULL ||
                target_statement->kind != MINIC_STATEMENT_LABEL) {
                return false;
            }
        }
        return true;
    }

    case MINIC_STATEMENT_RETURN:
''',
    "asm-goto-verifier-labels",
)

# RV64 target: named `%l[label]` references lower to the existing user-label
# statement identity. Dynamic `i` operands are intentionally represented by an
# unresolved external symbol until always-inline specialization exists; this is
# explicit and link-visible rather than silently substituting a wrong value.
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "#include <stdint.h>\n",
    "#include <inttypes.h>\n#include <stdint.h>\n",
    "asm-goto-inttypes",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "static bool validate_output(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {\n",
    r'''static const MinicInlineAsmLabel *find_named_label(const MinicInlineAsm *inline_asm,
                                                    const char *name,
                                                    size_t name_length) {
    size_t index;

    if (inline_asm == NULL || name == NULL || name_length == 0U) {
        return NULL;
    }
    for (index = 0U; index < inline_asm->label_count; ++index) {
        const MinicInlineAsmLabel *label;

        label = &inline_asm->labels[index];
        if (label->name != NULL && label->name_length == name_length &&
            memcmp(label->name, name, name_length) == 0) {
            return label;
        }
    }
    return NULL;
}

static bool validate_output(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
''',
    "asm-goto-label-lookup",
)
# validate_input gets inline_asm context to gate deferred `i` to asm-goto only.
p = Path("src/target/riscv64/codegen_inline_asm.c")
text = p.read_text()
old = r'''static bool validate_input(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || !constraint_is(operand, "r")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}
'''
new = r'''static bool validate_input(const MinicInlineAsm *inline_asm,
                           const MinicC0Program *program,
                           const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (inline_asm == NULL || program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        (!constraint_is(operand, "r") && !(inline_asm->is_goto && constraint_is(operand, "i")))) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}
'''
if text.count(old) != 1:
    raise SystemExit("asm-goto validate_input anchor mismatch")
text = text.replace(old, new, 1)
p.write_text(text)

replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "static bool template_operands_are_valid(const MinicInlineAsm *inline_asm, size_t operand_count) {\n",
    r'''static bool resolve_label_reference(const MinicInlineAsm *inline_asm,
                                    size_t *template_index,
                                    MinicStatementId *target_statement) {
    size_t index;
    size_t name_begin;
    size_t name_end;
    const MinicInlineAsmLabel *label;

    if (inline_asm == NULL || template_index == NULL || target_statement == NULL ||
        *template_index + 3U >= inline_asm->template_length ||
        inline_asm->template_text[*template_index] != '%' ||
        inline_asm->template_text[*template_index + 1U] != 'l' ||
        inline_asm->template_text[*template_index + 2U] != '[') {
        return false;
    }
    index = *template_index + 3U;
    name_begin = index;
    while (index < inline_asm->template_length && inline_asm->template_text[index] != ']') {
        index += 1U;
    }
    name_end = index;
    if (name_end == name_begin || name_end >= inline_asm->template_length) {
        return false;
    }
    label = find_named_label(
        inline_asm, inline_asm->template_text + name_begin, name_end - name_begin);
    if (label == NULL || label->target_statement == MINIC_STATEMENT_INVALID) {
        return false;
    }
    *target_statement = label->target_statement;
    *template_index = name_end;
    return true;
}

static bool template_operands_are_valid(const MinicInlineAsm *inline_asm, size_t operand_count) {
''',
    "asm-goto-template-label-resolver",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "        if (!resolve_template_reference(\n"
    "                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {\n"
    "            return false;\n"
    "        }\n"
    "        (void)operand_index;\n"
    "        (void)literal_percent;\n",
    "        if (index + 2U < inline_asm->template_length &&\n"
    "            inline_asm->template_text[index + 1U] == 'l' &&\n"
    "            inline_asm->template_text[index + 2U] == '[') {\n"
    "            MinicStatementId target_statement;\n\n"
    "            if (!resolve_label_reference(inline_asm, &index, &target_statement)) {\n"
    "                return false;\n"
    "            }\n"
    "            (void)target_statement;\n"
    "            continue;\n"
    "        }\n"
    "        if (!resolve_template_reference(\n"
    "                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {\n"
    "            return false;\n"
    "        }\n"
    "        (void)operand_index;\n"
    "        (void)literal_percent;\n",
    "asm-goto-template-validation",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "static bool emit_template(FILE *file, const MinicInlineAsm *inline_asm) {\n",
    r'''static bool emit_immediate_operand(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicInlineAsmOperand *operand,
                                   MinicInlineAsmId inline_asm_id,
                                   size_t operand_index) {
    const MinicExpression *expression;

    if (file == NULL || program == NULL || operand == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER && minic_type_is_integer(expression->type)) {
        return fprintf(file, "%" PRId64, expression->value.integer_value) >= 0;
    }
    return fprintf(file,
                   "__minic_deferred_asm_immediate_%zu_%zu",
                   (size_t)inline_asm_id,
                   operand_index) >= 0;
}

static bool emit_template(FILE *file,
                          const MinicC0Program *program,
                          const MinicInlineAsm *inline_asm,
                          MinicInlineAsmId inline_asm_id) {
''',
    "asm-goto-immediate-emitter",
)
# Replace the body fragment in emit_template around references.
p = Path("src/target/riscv64/codegen_inline_asm.c")
text = p.read_text()
old = r'''        if (!resolve_template_reference(
                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {
            return false;
        }
        if (literal_percent) {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        {
            const MinicInlineAsmOperand *operand;
            const char *register_name;

            operand = operand_at(inline_asm, operand_index);
            register_name = minic_riscv64_inline_asm_registers[operand_index];
            if (operand == NULL) {
                return false;
            }
            if (constraint_is(operand, "+A")) {
                if (fprintf(file, "(%s)", register_name) < 0) {
                    return false;
                }
            } else if (fputs(register_name, file) == EOF) {
                return false;
            }
        }
'''
new = r'''        if (index + 2U < inline_asm->template_length &&
            inline_asm->template_text[index + 1U] == 'l' &&
            inline_asm->template_text[index + 2U] == '[') {
            MinicStatementId target_statement;

            if (!resolve_label_reference(inline_asm, &index, &target_statement) ||
                fprintf(file, ".Luser_%zu", (size_t)target_statement) < 0) {
                return false;
            }
            continue;
        }
        if (!resolve_template_reference(
                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {
            return false;
        }
        if (literal_percent) {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        {
            const MinicInlineAsmOperand *operand;
            const char *register_name;

            operand = operand_at(inline_asm, operand_index);
            register_name = minic_riscv64_inline_asm_registers[operand_index];
            if (operand == NULL) {
                return false;
            }
            if (constraint_is(operand, "i")) {
                if (!emit_immediate_operand(
                        file, program, operand, inline_asm_id, operand_index)) {
                    return false;
                }
            } else if (constraint_is(operand, "+A")) {
                if (fprintf(file, "(%s)", register_name) < 0) {
                    return false;
                }
            } else if (fputs(register_name, file) == EOF) {
                return false;
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit("asm-goto emit-template reference anchor mismatch")
text = text.replace(old, new, 1)
p.write_text(text)

# Main emitter: no raw-template shortcut when labels need substitution; `i`
# inputs do not evaluate at runtime, and dynamic ones advertise their deferred
# specialization dependency as an unresolved external symbol.
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "    if (inline_asm->output_count == 0U && inline_asm->input_count == 0U) {\n"
    "        return fprintf(file, \"  %s\\n\", inline_asm->template_text) >= 0;\n"
    "    }\n",
    "    if (inline_asm->output_count == 0U && inline_asm->input_count == 0U &&\n"
    "        inline_asm->label_count == 0U) {\n"
    "        return fprintf(file, \"  %s\\n\", inline_asm->template_text) >= 0;\n"
    "    }\n",
    "asm-goto-raw-template-shortcut",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "    if (operand_count == 0U || operand_count > MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS ||\n"
    "        !template_operands_are_valid(inline_asm, operand_count)) {\n",
    "    if (operand_count > MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS ||\n"
    "        !template_operands_are_valid(inline_asm, operand_count)) {\n",
    "asm-goto-zero-register-operands",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "        if (!validate_input(program, &inline_asm->inputs[index])) {\n",
    "        if (!validate_input(inline_asm, program, &inline_asm->inputs[index])) {\n",
    "asm-goto-input-validation-call",
)
# Insert explicit deferred-symbol declarations before stack allocation.
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "    if (operand_count > (SIZE_MAX - 15U) / 8U) {\n",
    r'''    for (index = 0U; index < inline_asm->input_count; ++index) {
        const MinicInlineAsmOperand *operand;
        const MinicExpression *expression;
        size_t operand_index;

        operand = &inline_asm->inputs[index];
        if (!constraint_is(operand, "i")) {
            continue;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        operand_index = inline_asm->output_count + index;
        if (expression == NULL) {
            return false;
        }
        if (expression->kind != MINIC_EXPRESSION_INTEGER &&
            fprintf(file,
                    "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                    "  .extern __minic_deferred_asm_immediate_%zu_%zu\n",
                    (size_t)statement->inline_asm_id,
                    operand_index) < 0) {
            return false;
        }
    }

    if (operand_count > (SIZE_MAX - 15U) / 8U) {
''',
    "asm-goto-deferred-symbol-directive",
)
# Skip runtime evaluation/load for immediate inputs.
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "        operand = &inline_asm->inputs[index];\n"
    "        operand_index = inline_asm->output_count + index;\n"
    "        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||\n",
    "        operand = &inline_asm->inputs[index];\n"
    "        operand_index = inline_asm->output_count + index;\n"
    "        if (constraint_is(operand, \"i\")) {\n"
    "            continue;\n"
    "        }\n"
    "        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||\n",
    "asm-goto-skip-immediate-evaluation",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "        operand_index = inline_asm->output_count + index;\n"
    "        if (!minic_riscv64_emit_sp_load64(\n"
    "                file, minic_riscv64_inline_asm_registers[operand_index], operand_index * 8U)) {\n",
    "        operand_index = inline_asm->output_count + index;\n"
    "        if (constraint_is(&inline_asm->inputs[index], \"i\")) {\n"
    "            continue;\n"
    "        }\n"
    "        if (!minic_riscv64_emit_sp_load64(\n"
    "                file, minic_riscv64_inline_asm_registers[operand_index], operand_index * 8U)) {\n",
    "asm-goto-skip-immediate-register-load",
)
replace_once(
    "src/target/riscv64/codegen_inline_asm.c",
    "    if (!emit_template(file, inline_asm)) {\n",
    "    if (!emit_template(file, program, inline_asm, statement->inline_asm_id)) {\n",
    "asm-goto-template-call",
)
