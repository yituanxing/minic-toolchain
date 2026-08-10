#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    target.write_text(text.replace(old, new, 1))


# Keep inline assembly payload out of every statement node. The Program owns a
# stable InlineAsmId and a dedicated pool, matching the same stable-ID direction
# used by expressions/functions/records and leaving room for operands/constraints.
replace_once(
    "src/frontend/ast.h",
    "typedef size_t MinicGlobalObjectId;\n",
    "typedef size_t MinicGlobalObjectId;\ntypedef size_t MinicInlineAsmId;\n",
    "inline-asm-id-type",
)
replace_once(
    "src/frontend/ast.h",
    "#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)\n",
    "#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)\n#define MINIC_INLINE_ASM_INVALID ((MinicInlineAsmId) - 1)\n",
    "inline-asm-invalid-id",
)
replace_once(
    "src/frontend/ast.h",
    "    MINIC_STATEMENT_EXPRESSION,\n    MINIC_STATEMENT_RETURN,\n",
    "    MINIC_STATEMENT_EXPRESSION,\n    MINIC_STATEMENT_INLINE_ASM,\n    MINIC_STATEMENT_RETURN,\n",
    "inline-asm-statement-kind",
)
replace_once(
    "src/frontend/ast.h",
    "    MinicStatementId target_statement;\n    MinicBlockId then_block;\n",
    "    MinicStatementId target_statement;\n    MinicInlineAsmId inline_asm_id;\n    MinicBlockId then_block;\n",
    "inline-asm-statement-id",
)
replace_once(
    "src/frontend/ast.h",
    "typedef struct MinicBlock {\n",
    r'''typedef struct MinicInlineAsm {
    char *template_text;
    size_t template_length;
    size_t output_count;
    size_t input_count;
    size_t clobber_count;
    bool is_volatile;
    bool has_memory_clobber;
} MinicInlineAsm;

typedef struct MinicBlock {
''',
    "inline-asm-record",
)
replace_once(
    "src/frontend/ast.h",
    "    MinicStatement *statements;\n    size_t statement_count;\n    size_t statement_capacity;\n\n",
    "    MinicStatement *statements;\n    size_t statement_count;\n    size_t statement_capacity;\n\n    MinicInlineAsm *inline_asms;\n    size_t inline_asm_count;\n    size_t inline_asm_capacity;\n\n",
    "inline-asm-program-pool",
)
replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id);\n",
    """bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id);
bool minic_c0_program_add_inline_asm(MinicC0Program *program,
                                     const char *template_text,
                                     size_t template_length,
                                     bool is_volatile,
                                     bool has_memory_clobber,
                                     MinicInlineAsmId *inline_asm_id);
""",
    "inline-asm-add-prototype",
)
replace_once(
    "src/frontend/ast.h",
    "const MinicBlock *minic_c0_program_block(const MinicC0Program *program, MinicBlockId block_id);\n",
    """const MinicBlock *minic_c0_program_block(const MinicC0Program *program, MinicBlockId block_id);
const MinicInlineAsm *minic_c0_program_inline_asm(const MinicC0Program *program,
                                                  MinicInlineAsmId inline_asm_id);
""",
    "inline-asm-get-prototype",
)

# Program lifetime owns decoded templates. Future operand/constraint slices can
# grow inside this dedicated pool without inflating all MinicStatement nodes.
replace_once(
    "src/frontend/ast.c",
    "    for (index = 0U; index < program->block_count; ++index) {\n        free(program->blocks[index].statements);\n    }\n",
    """    for (index = 0U; index < program->block_count; ++index) {
        free(program->blocks[index].statements);
    }
    for (index = 0U; index < program->inline_asm_count; ++index) {
        free(program->inline_asms[index].template_text);
    }
""",
    "inline-asm-destroy-items",
)
replace_once(
    "src/frontend/ast.c",
    "    free(program->statements);\n    free(program->blocks);\n",
    "    free(program->statements);\n    free(program->inline_asms);\n    free(program->blocks);\n",
    "inline-asm-destroy-pool",
)
replace_once(
    "src/frontend/ast.c",
    "bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id) {\n",
    r'''bool minic_c0_program_add_inline_asm(MinicC0Program *program,
                                     const char *template_text,
                                     size_t template_length,
                                     bool is_volatile,
                                     bool has_memory_clobber,
                                     MinicInlineAsmId *inline_asm_id) {
    MinicInlineAsm inline_asm;

    if (program == NULL || template_text == NULL || inline_asm_id == NULL) {
        return false;
    }
    if (!minic_grow_array((void **)&program->inline_asms,
                          &program->inline_asm_capacity,
                          program->inline_asm_count,
                          sizeof(*program->inline_asms))) {
        return false;
    }
    (void)memset(&inline_asm, 0, sizeof(inline_asm));
    inline_asm.template_text = minic_copy_name(template_text, template_length);
    if (inline_asm.template_text == NULL) {
        return false;
    }
    inline_asm.template_length = template_length;
    inline_asm.is_volatile = is_volatile;
    inline_asm.has_memory_clobber = has_memory_clobber;
    inline_asm.clobber_count = has_memory_clobber ? 1U : 0U;
    *inline_asm_id = program->inline_asm_count;
    program->inline_asms[program->inline_asm_count] = inline_asm;
    program->inline_asm_count += 1U;
    return true;
}

bool minic_c0_program_add_block(MinicC0Program *program, MinicBlockId *block_id) {
''',
    "inline-asm-program-add",
)
path = Path("src/frontend/ast.c")
text = path.read_text()
text += r'''

const MinicInlineAsm *minic_c0_program_inline_asm(const MinicC0Program *program,
                                                  MinicInlineAsmId inline_asm_id) {
    if (program == NULL || inline_asm_id >= program->inline_asm_count) {
        return NULL;
    }
    return &program->inline_asms[inline_asm_id];
}
'''
path.write_text(text)

# Reuse the parser's existing escape decoder for assembly templates. This keeps
# adjacent string concatenation and C escape semantics in one subsystem instead
# of inventing an asm-specific string parser.
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id);\n",
    """bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id);
bool minic_parser_parse_string_text(MinicParser *parser,
                                    char **text,
                                    size_t *length,
                                    MinicSourceSpan *span);
""",
    "inline-asm-string-prototype",
)
replace_once(
    "src/frontend/parser_string.c",
    "#include <stdio.h>\n#include <string.h>\n",
    "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n",
    "inline-asm-string-stdlib",
)
path = Path("src/frontend/parser_string.c")
text = path.read_text()
anchor = "bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id) {\n"
helper = r'''bool minic_parser_parse_string_text(MinicParser *parser,
                                    char **text,
                                    size_t *length,
                                    MinicSourceSpan *span) {
    MinicParser probe;
    MinicSourceSpan combined_span;
    char *buffer;
    size_t total_length;
    size_t decoded_length;
    size_t output;

    if (parser == NULL || text == NULL || length == NULL || span == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    probe = *parser;
    combined_span = probe.current.span;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(&probe, probe.current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            minic_parser_error(parser, "inline assembly string is too long");
            return false;
        }
        total_length += decoded_length;
        combined_span.end = probe.current.span.end;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX) {
        minic_parser_error(parser, "inline assembly string is too long");
        return false;
    }
    buffer = (char *)malloc(total_length + 1U);
    if (buffer == NULL) {
        minic_parser_error(parser, "out of memory while decoding inline assembly string");
        return false;
    }

    output = 0U;
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t cursor;
        size_t end;

        cursor = parser->current.span.begin.offset + 1U;
        end = parser->current.span.end.offset - 1U;
        while (cursor < end) {
            int value;

            if (parser->source[cursor] == '\\') {
                cursor += 1U;
                if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                    free(buffer);
                    minic_parser_error(parser, "unsupported inline assembly string escape");
                    return false;
                }
            } else {
                value = (int)(unsigned char)parser->source[cursor];
                cursor += 1U;
            }
            if (value == 0) {
                free(buffer);
                minic_parser_error(parser, "inline assembly template cannot contain NUL");
                return false;
            }
            buffer[output] = (char)value;
            output += 1U;
        }
        if (!minic_parser_advance(parser)) {
            free(buffer);
            return false;
        }
    }
    buffer[output] = '\0';
    *text = buffer;
    *length = output;
    *span = combined_span;
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"inline-asm-string-helper: expected one anchor, found {text.count(anchor)}")
path.write_text(text.replace(anchor, helper + anchor, 1))

# Parse the current Linux extended-asm subset as a first-class statement. Empty
# outputs/inputs and the compiler-level "memory" clobber are supported now; any
# operand constraint or register clobber is rejected explicitly until the
# TargetConstraint consumer is introduced.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
anchor = "static bool token_starts_local_declaration(const MinicParser *parser) {\n"
helper = r'''static bool inline_asm_identifier_is(const MinicParser *parser, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(name) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool current_is_gnu_asm(const MinicParser *parser) {
    return inline_asm_identifier_is(parser, "asm") || inline_asm_identifier_is(parser, "__asm") ||
           inline_asm_identifier_is(parser, "__asm__");
}

static bool current_is_gnu_volatile(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||
           inline_asm_identifier_is(parser, "__volatile") ||
           inline_asm_identifier_is(parser, "__volatile__");
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
                           "GNU asm operand substitutions require TargetConstraint support");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            free(template_text);
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COLON) {
            free(template_text);
            minic_parser_error(parser, "GNU asm output operands are not supported yet");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            free(template_text);
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COLON) {
            free(template_text);
            minic_parser_error(parser, "GNU asm input operands are not supported yet");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            free(template_text);
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
                free(template_text);
                free(clobber);
                return false;
            }
            if (clobber_length == 6U && memcmp(clobber, "memory", 6U) == 0) {
                has_memory_clobber = true;
            } else {
                free(template_text);
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
                free(template_text);
                return false;
            }
        }
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_INLINE_ASM;
    statement.span.begin = begin;
    statement.span.end = parser->current.span.end;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.inline_asm_id = MINIC_INLINE_ASM_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm") ||
        !minic_c0_program_add_inline_asm(parser->program,
                                         template_text,
                                         template_length,
                                         is_volatile,
                                         has_memory_clobber,
                                         &inline_asm_id)) {
        free(template_text);
        minic_parser_error(parser, "cannot store GNU inline assembly");
        return false;
    }
    free(template_text);
    statement.inline_asm_id = inline_asm_id;
    if (!minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU asm")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"inline-asm-parser-helper: expected one anchor, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)
# parser_statement.c already uses malloc/free indirectly only after this feature.
text = text.replace("#include <stdio.h>\n#include <string.h>\n",
                    "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n", 1)
dispatch = """    if (current_identifier_is_goto(parser)) {
        return parse_goto(parser);
    }
"""
replacement = """    if (current_is_gnu_asm(parser)) {
        return parse_gnu_inline_asm_statement(parser);
    }
    if (current_identifier_is_goto(parser)) {
        return parse_goto(parser);
    }
"""
if text.count(dispatch) != 1:
    raise SystemExit(f"inline-asm-dispatch: expected one anchor, found {text.count(dispatch)}")
path.write_text(text.replace(dispatch, replacement, 1))

# Verifier tracks pool lifetime and statement ownership; the current subset has
# no explicit operands and at most the semantic memory clobber.
replace_once(
    "src/frontend/ast_verifier.c",
    "    case MINIC_STATEMENT_EXPRESSION:\n        return expression != NULL;\n",
    """    case MINIC_STATEMENT_EXPRESSION:
        return expression != NULL;
    case MINIC_STATEMENT_INLINE_ASM: {
        const MinicInlineAsm *inline_asm;

        inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
        return inline_asm != NULL && inline_asm->template_text != NULL &&
               inline_asm->output_count == 0U && inline_asm->input_count == 0U &&
               inline_asm->clobber_count <= 1U &&
               inline_asm->clobber_count == (inline_asm->has_memory_clobber ? 1U : 0U) &&
               statement->target_expression == MINIC_EXPRESSION_INVALID &&
               statement->expression == MINIC_EXPRESSION_INVALID &&
               statement->target_statement == MINIC_STATEMENT_INVALID &&
               statement->then_block == MINIC_BLOCK_INVALID &&
               statement->else_block == MINIC_BLOCK_INVALID;
    }
""",
    "inline-asm-verifier-statement",
)
replace_once(
    "src/frontend/ast_verifier.c",
    "           storage_is_valid(\n               program->statements, program->statement_count, program->statement_capacity) &&\n",
    """           storage_is_valid(
               program->statements, program->statement_count, program->statement_capacity) &&
           storage_is_valid(
               program->inline_asms, program->inline_asm_count, program->inline_asm_capacity) &&
""",
    "inline-asm-verifier-storage",
)

# The direct AST->RV64 bootstrap emits literal no-operand templates in source
# order. `volatile` and `memory` are preserved metadata now; when Core IR arrives
# they become side-effect/memory-effect flags that block invalid motion/removal.
replace_once(
    "src/target/riscv64/codegen_statement.c",
    "        case MINIC_STATEMENT_EXPRESSION:\n        case MINIC_STATEMENT_RETURN:\n",
    "        case MINIC_STATEMENT_EXPRESSION:\n        case MINIC_STATEMENT_INLINE_ASM:\n        case MINIC_STATEMENT_RETURN:\n",
    "inline-asm-switch-collector",
)
replace_once(
    "src/target/riscv64/codegen_statement.c",
    """    case MINIC_STATEMENT_EXPRESSION:
        return statement->expression != MINIC_EXPRESSION_INVALID &&
               minic_riscv64_emit_expression(file, program, function, statement->expression);

    case MINIC_STATEMENT_RETURN:
""",
    """    case MINIC_STATEMENT_EXPRESSION:
        return statement->expression != MINIC_EXPRESSION_INVALID &&
               minic_riscv64_emit_expression(file, program, function, statement->expression);

    case MINIC_STATEMENT_INLINE_ASM: {
        const MinicInlineAsm *inline_asm;

        inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
        return inline_asm != NULL && inline_asm->template_text != NULL &&
               inline_asm->output_count == 0U && inline_asm->input_count == 0U &&
               fprintf(file, "  %s\n", inline_asm->template_text) >= 0;
    }

    case MINIC_STATEMENT_RETURN:
""",
    "inline-asm-codegen-statement",
)

print("staged first-class GNU inline asm: decoded template, volatile/memory metadata, AST verifier and RV64 emission")
