#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_range(text: str, start: str, end: str, new: str, label: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start anchor not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end anchor not found")
    return text[:begin] + new + text[finish:]


root = Path(__file__).resolve().parents[2]

# Program-owned asm operands gain an optional symbolic name.
path = root / "src/frontend/ast.h"
text = path.read_text()
text = replace_once(
    text,
    '''typedef struct MinicInlineAsmOperand {\n    char *constraint_text;\n    size_t constraint_length;\n    MinicExpressionId expression;\n    MinicInlineAsmOperandAccess access;\n} MinicInlineAsmOperand;\n''',
    '''typedef struct MinicInlineAsmOperand {\n    char *name;\n    size_t name_length;\n    char *constraint_text;\n    size_t constraint_length;\n    MinicExpressionId expression;\n    MinicInlineAsmOperandAccess access;\n} MinicInlineAsmOperand;\n''',
    "asm-operand-name-storage",
)
text = replace_once(
    text,
    '''bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,\n                                            MinicInlineAsmId inline_asm_id,\n                                            const char *constraint_text,\n                                            size_t constraint_length,\n                                            MinicExpressionId expression,\n                                            MinicInlineAsmOperandAccess access);\nbool minic_c0_program_add_inline_asm_input(MinicC0Program *program,\n                                           MinicInlineAsmId inline_asm_id,\n                                           const char *constraint_text,\n                                           size_t constraint_length,\n                                           MinicExpressionId expression);\n''',
    '''bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,\n                                            MinicInlineAsmId inline_asm_id,\n                                            const char *name,\n                                            size_t name_length,\n                                            const char *constraint_text,\n                                            size_t constraint_length,\n                                            MinicExpressionId expression,\n                                            MinicInlineAsmOperandAccess access);\nbool minic_c0_program_add_inline_asm_input(MinicC0Program *program,\n                                           MinicInlineAsmId inline_asm_id,\n                                           const char *name,\n                                           size_t name_length,\n                                           const char *constraint_text,\n                                           size_t constraint_length,\n                                           MinicExpressionId expression);\n''',
    "asm-operand-name-api",
)
path.write_text(text)

# Program owns/copies operand names and rejects duplicate names across outputs+inputs.
path = root / "src/frontend/ast.c"
text = path.read_text()
text = replace_once(
    text,
    '''        for (operand_index = 0U; operand_index < program->inline_asms[index].output_count;\n             ++operand_index) {\n            free(program->inline_asms[index].outputs[operand_index].constraint_text);\n        }\n        for (operand_index = 0U; operand_index < program->inline_asms[index].input_count;\n             ++operand_index) {\n            free(program->inline_asms[index].inputs[operand_index].constraint_text);\n        }\n''',
    '''        for (operand_index = 0U; operand_index < program->inline_asms[index].output_count;\n             ++operand_index) {\n            free(program->inline_asms[index].outputs[operand_index].name);\n            free(program->inline_asms[index].outputs[operand_index].constraint_text);\n        }\n        for (operand_index = 0U; operand_index < program->inline_asms[index].input_count;\n             ++operand_index) {\n            free(program->inline_asms[index].inputs[operand_index].name);\n            free(program->inline_asms[index].inputs[operand_index].constraint_text);\n        }\n''',
    "asm-operand-name-destroy",
)
new_operand_apis = r'''static bool inline_asm_operand_name_matches(const MinicInlineAsmOperand *operand,
                                            const char *name,
                                            size_t name_length) {
    return operand != NULL && operand->name != NULL && name != NULL &&
           operand->name_length == name_length && memcmp(operand->name, name, name_length) == 0;
}

static bool inline_asm_operand_name_is_available(const MinicInlineAsm *inline_asm,
                                                 const char *name,
                                                 size_t name_length) {
    size_t index;

    if (inline_asm == NULL || name_length == 0U) {
        return name == NULL && name_length == 0U;
    }
    if (name == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (inline_asm_operand_name_matches(&inline_asm->outputs[index], name, name_length)) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        if (inline_asm_operand_name_matches(&inline_asm->inputs[index], name, name_length)) {
            return false;
        }
    }
    return true;
}

static bool initialize_inline_asm_operand(MinicInlineAsmOperand *operand,
                                          const char *name,
                                          size_t name_length,
                                          const char *constraint_text,
                                          size_t constraint_length,
                                          MinicExpressionId expression,
                                          MinicInlineAsmOperandAccess access) {
    if (operand == NULL || constraint_text == NULL || constraint_length == 0U ||
        ((name == NULL) != (name_length == 0U))) {
        return false;
    }
    (void)memset(operand, 0, sizeof(*operand));
    if (name_length != 0U) {
        operand->name = minic_copy_name(name, name_length);
        if (operand->name == NULL) {
            return false;
        }
        operand->name_length = name_length;
    }
    operand->constraint_text = minic_copy_name(constraint_text, constraint_length);
    if (operand->constraint_text == NULL) {
        free(operand->name);
        operand->name = NULL;
        operand->name_length = 0U;
        return false;
    }
    operand->constraint_length = constraint_length;
    operand->expression = expression;
    operand->access = access;
    return true;
}

bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,
                                            MinicInlineAsmId inline_asm_id,
                                            const char *name,
                                            size_t name_length,
                                            const char *constraint_text,
                                            size_t constraint_length,
                                            MinicExpressionId expression,
                                            MinicInlineAsmOperandAccess access) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmOperand operand;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || constraint_text == NULL ||
        constraint_length == 0U || expression >= program->expression_count ||
        (access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
         access != MINIC_INLINE_ASM_OPERAND_READ_WRITE)) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    if (!inline_asm_operand_name_is_available(inline_asm, name, name_length) ||
        !minic_grow_array((void **)&inline_asm->outputs,
                          &inline_asm->output_capacity,
                          inline_asm->output_count,
                          sizeof(*inline_asm->outputs)) ||
        !initialize_inline_asm_operand(&operand,
                                       name,
                                       name_length,
                                       constraint_text,
                                       constraint_length,
                                       expression,
                                       access)) {
        return false;
    }
    inline_asm->outputs[inline_asm->output_count] = operand;
    inline_asm->output_count += 1U;
    return true;
}

bool minic_c0_program_add_inline_asm_input(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
                                           const char *name,
                                           size_t name_length,
                                           const char *constraint_text,
                                           size_t constraint_length,
                                           MinicExpressionId expression) {
    MinicInlineAsm *inline_asm;
    MinicInlineAsmOperand operand;

    if (program == NULL || inline_asm_id >= program->inline_asm_count || constraint_text == NULL ||
        constraint_length == 0U || expression >= program->expression_count) {
        return false;
    }
    inline_asm = &program->inline_asms[inline_asm_id];
    if (!inline_asm_operand_name_is_available(inline_asm, name, name_length) ||
        !minic_grow_array((void **)&inline_asm->inputs,
                          &inline_asm->input_capacity,
                          inline_asm->input_count,
                          sizeof(*inline_asm->inputs)) ||
        !initialize_inline_asm_operand(&operand,
                                       name,
                                       name_length,
                                       constraint_text,
                                       constraint_length,
                                       expression,
                                       MINIC_INLINE_ASM_OPERAND_READ_ONLY)) {
        return false;
    }
    inline_asm->inputs[inline_asm->input_count] = operand;
    inline_asm->input_count += 1U;
    return true;
}

'''
text = replace_range(
    text,
    "bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,",
    "bool minic_c0_program_set_inline_asm_memory_clobber",
    new_operand_apis,
    "asm-named-operand-apis",
)
path.write_text(text)

# Parser accepts optional [name] before each output/input constraint.
path = root / "src/frontend/parser_statement.c"
text = path.read_text()
name_helper = r'''static bool parse_gnu_inline_asm_operand_name(MinicParser *parser,
                                              const char **name,
                                              size_t *name_length) {
    if (parser == NULL || name == NULL || name_length == NULL) {
        return false;
    }
    *name = NULL;
    *name_length = 0U;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return true;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected GNU asm operand name after '['");
        return false;
    }
    *name = parser->source + parser->current.span.begin.offset;
    *name_length = minic_parser_span_length(parser->current.span);
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']' after GNU asm operand name")) {
        return false;
    }
    return true;
}

'''
text = replace_once(
    text,
    "static bool parse_gnu_inline_asm_output(MinicParser *parser, MinicInlineAsmId inline_asm_id) {\n",
    name_helper + "static bool parse_gnu_inline_asm_output(MinicParser *parser, MinicInlineAsmId inline_asm_id) {\n",
    "asm-operand-name-parser",
)
old_output_prefix = '''    MinicInlineAsmOperandAccess access;\n    MinicSourceSpan constraint_span;\n    char *constraint;\n    size_t constraint_length;\n\n    constraint = NULL;\n    constraint_length = 0U;\n    if (!minic_parser_parse_string_text(\n            parser, &constraint, &constraint_length, &constraint_span)) {\n'''
new_output_prefix = '''    MinicInlineAsmOperandAccess access;\n    MinicSourceSpan constraint_span;\n    const char *name;\n    char *constraint;\n    size_t constraint_length;\n    size_t name_length;\n\n    constraint = NULL;\n    constraint_length = 0U;\n    name = NULL;\n    name_length = 0U;\n    if (!parse_gnu_inline_asm_operand_name(parser, &name, &name_length) ||\n        !minic_parser_parse_string_text(\n            parser, &constraint, &constraint_length, &constraint_span)) {\n'''
text = replace_once(text, old_output_prefix, new_output_prefix, "asm-output-name-prefix")
text = replace_once(
    text,
    '''    if (!minic_c0_program_add_inline_asm_output(\n            parser->program, inline_asm_id, constraint, constraint_length, operand_id, access)) {\n''',
    '''    if (!minic_c0_program_add_inline_asm_output(parser->program,\n                                                    inline_asm_id,\n                                                    name,\n                                                    name_length,\n                                                    constraint,\n                                                    constraint_length,\n                                                    operand_id,\n                                                    access)) {\n''',
    "asm-output-name-storage",
)
old_input_prefix = '''    MinicSourceSpan constraint_span;\n    char *constraint;\n    size_t constraint_length;\n\n    constraint = NULL;\n    constraint_length = 0U;\n    if (!minic_parser_parse_string_text(\n            parser, &constraint, &constraint_length, &constraint_span)) {\n'''
new_input_prefix = '''    MinicSourceSpan constraint_span;\n    const char *name;\n    char *constraint;\n    size_t constraint_length;\n    size_t name_length;\n\n    constraint = NULL;\n    constraint_length = 0U;\n    name = NULL;\n    name_length = 0U;\n    if (!parse_gnu_inline_asm_operand_name(parser, &name, &name_length) ||\n        !minic_parser_parse_string_text(\n            parser, &constraint, &constraint_length, &constraint_span)) {\n'''
# The input function contains this same variable block exactly once after the output edit.
input_start = text.find("static bool parse_gnu_inline_asm_input")
if input_start < 0:
    raise SystemExit("asm-input-name-prefix: input parser not found")
input_tail = text[input_start:]
if input_tail.count(old_input_prefix) != 1:
    raise SystemExit(f"asm-input-name-prefix: expected one anchor, found {input_tail.count(old_input_prefix)}")
input_tail = input_tail.replace(old_input_prefix, new_input_prefix, 1)
text = text[:input_start] + input_tail
text = replace_once(
    text,
    '''    if (!minic_c0_program_add_inline_asm_input(\n            parser->program, inline_asm_id, constraint, constraint_length, operand_id)) {\n''',
    '''    if (!minic_c0_program_add_inline_asm_input(parser->program,\n                                                   inline_asm_id,\n                                                   name,\n                                                   name_length,\n                                                   constraint,\n                                                   constraint_length,\n                                                   operand_id)) {\n''',
    "asm-input-name-storage",
)
path.write_text(text)

# Verifier checks optional name storage consistency, not target-specific constraints.
path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
text = replace_once(
    text,
    '''            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||\n                operand_expression == NULL ||\n''',
    '''            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||\n                ((operand->name == NULL) != (operand->name_length == 0U)) ||\n                operand_expression == NULL ||\n''',
    "asm-output-name-verifier",
)
# Apply the same invariant to the input loop only after output was replaced.
input_loop = text.find("for (operand_index = 0U; operand_index < inline_asm->input_count")
if input_loop < 0:
    raise SystemExit("asm-input-name-verifier: input loop not found")
needle = '''            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||\n                operand_expression == NULL ||\n'''
pos = text.find(needle, input_loop)
if pos < 0:
    raise SystemExit("asm-input-name-verifier: condition not found")
text = text[:pos] + needle.replace(
    "                operand_expression == NULL ||\n",
    "                ((operand->name == NULL) != (operand->name_length == 0U)) ||\n                operand_expression == NULL ||\n",
) + text[pos + len(needle):]
path.write_text(text)

# RV64 target: resolve positional or named placeholders and accept early-clobber register outputs.
path = root / "src/target/riscv64/codegen_inline_asm.c"
path.write_text(r'''#include "target/riscv64/codegen_internal.h"

#include <stdint.h>
#include <string.h>

#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U

static const char *const minic_riscv64_inline_asm_registers[] = {
    "t0", "t1", "t3", "t4", "t5", "t6"};

static bool constraint_is(const MinicInlineAsmOperand *operand, const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static const MinicInlineAsmOperand *operand_at(const MinicInlineAsm *inline_asm,
                                               size_t operand_index) {
    if (inline_asm == NULL) {
        return NULL;
    }
    if (operand_index < inline_asm->output_count) {
        return &inline_asm->outputs[operand_index];
    }
    operand_index -= inline_asm->output_count;
    if (operand_index >= inline_asm->input_count) {
        return NULL;
    }
    return &inline_asm->inputs[operand_index];
}

static bool operand_name_matches(const MinicInlineAsmOperand *operand,
                                 const char *name,
                                 size_t name_length) {
    return operand != NULL && operand->name != NULL && name != NULL &&
           operand->name_length == name_length && memcmp(operand->name, name, name_length) == 0;
}

static bool find_named_operand(const MinicInlineAsm *inline_asm,
                               const char *name,
                               size_t name_length,
                               size_t *operand_index) {
    size_t index;
    size_t operand_count;

    if (inline_asm == NULL || name == NULL || name_length == 0U || operand_index == NULL ||
        inline_asm->output_count > SIZE_MAX - inline_asm->input_count) {
        return false;
    }
    operand_count = inline_asm->output_count + inline_asm->input_count;
    for (index = 0U; index < operand_count; ++index) {
        if (operand_name_matches(operand_at(inline_asm, index), name, name_length)) {
            *operand_index = index;
            return true;
        }
    }
    return false;
}

static bool validate_output(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (constraint_is(operand, "+A")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;
    }
    if (constraint_is(operand, "=r") || constraint_is(operand, "=&r")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
               expression->kind == MINIC_EXPRESSION_LOCAL &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
    return false;
}

static bool validate_input(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL ||
        operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY || !constraint_is(operand, "r")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}

static bool resolve_template_reference(const MinicInlineAsm *inline_asm,
                                       size_t operand_count,
                                       size_t *template_index,
                                       size_t *operand_index,
                                       bool *literal_percent) {
    size_t index;
    unsigned char ch;

    if (inline_asm == NULL || template_index == NULL || operand_index == NULL ||
        literal_percent == NULL || *template_index >= inline_asm->template_length ||
        inline_asm->template_text[*template_index] != '%') {
        return false;
    }
    index = *template_index + 1U;
    if (index >= inline_asm->template_length) {
        return false;
    }
    ch = (unsigned char)inline_asm->template_text[index];
    if (ch == '%') {
        *template_index = index;
        *literal_percent = true;
        *operand_index = 0U;
        return true;
    }
    *literal_percent = false;
    if (ch >= '0' && ch <= '9') {
        *operand_index = (size_t)(ch - '0');
        *template_index = index;
        return *operand_index < operand_count;
    }
    if (ch == '[') {
        size_t name_begin;
        size_t name_end;

        name_begin = index + 1U;
        name_end = name_begin;
        while (name_end < inline_asm->template_length &&
               inline_asm->template_text[name_end] != ']') {
            name_end += 1U;
        }
        if (name_end == name_begin || name_end >= inline_asm->template_length ||
            !find_named_operand(inline_asm,
                                inline_asm->template_text + name_begin,
                                name_end - name_begin,
                                operand_index)) {
            return false;
        }
        *template_index = name_end;
        return true;
    }
    return false;
}

static bool template_operands_are_valid(const MinicInlineAsm *inline_asm, size_t operand_count) {
    size_t index;

    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        bool literal_percent;

        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (!resolve_template_reference(
                inline_asm, operand_count, &index, &operand_index, &literal_percent)) {
            return false;
        }
        (void)operand_index;
        (void)literal_percent;
    }
    return true;
}

static bool emit_template(FILE *file, const MinicInlineAsm *inline_asm) {
    size_t operand_count;
    size_t index;

    operand_count = inline_asm->output_count + inline_asm->input_count;
    if (!template_operands_are_valid(inline_asm, operand_count) || fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        bool literal_percent;

        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
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
            if (constraint_is(operand, "+A")) {
                if (fprintf(file, "(%s)", register_name) < 0) {
                    return false;
                }
            } else if (fputs(register_name, file) == EOF) {
                return false;
            }
        }
    }
    return fputc('\n', file) != EOF;
}

bool minic_riscv64_emit_inline_asm(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicStatement *statement) {
    const MinicInlineAsm *inline_asm;
    size_t operand_count;
    size_t temporary_size;
    size_t index;

    if (file == NULL || program == NULL || function == NULL || statement == NULL) {
        return false;
    }
    inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    if (inline_asm->output_count == 0U && inline_asm->input_count == 0U) {
        return fprintf(file, "  %s\n", inline_asm->template_text) >= 0;
    }
    if (inline_asm->output_count == 1U && inline_asm->input_count == 0U &&
        inline_asm->template_length == 0U && constraint_is(&inline_asm->outputs[0], "+rm") &&
        inline_asm->outputs[0].access == MINIC_INLINE_ASM_OPERAND_READ_WRITE) {
        return true;
    }

    if (inline_asm->output_count > SIZE_MAX - inline_asm->input_count) {
        return false;
    }
    operand_count = inline_asm->output_count + inline_asm->input_count;
    if (operand_count == 0U || operand_count > MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS ||
        !template_operands_are_valid(inline_asm, operand_count)) {
        return false;
    }
    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (!validate_output(program, &inline_asm->outputs[index])) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        if (!validate_input(program, &inline_asm->inputs[index])) {
            return false;
        }
    }

    if (operand_count > (SIZE_MAX - 15U) / 8U) {
        return false;
    }
    temporary_size = (operand_count * 8U + 15U) & ~(size_t)15U;
    if (!minic_riscv64_emit_stack_allocate(file, temporary_size)) {
        return false;
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicInlineAsmOperand *operand;

        operand = &inline_asm->outputs[index];
        if (constraint_is(operand, "+A")) {
            if (!minic_riscv64_emit_lvalue_address(file, program, function, operand->expression) ||
                !minic_riscv64_emit_sp_store64(file, "a0", index * 8U)) {
                return false;
            }
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        const MinicInlineAsmOperand *operand;
        size_t operand_index;

        operand = &inline_asm->inputs[index];
        operand_index = inline_asm->output_count + index;
        if (!minic_riscv64_emit_expression(file, program, function, operand->expression) ||
            !minic_riscv64_emit_sp_store64(file, "a0", operand_index * 8U)) {
            return false;
        }
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (constraint_is(&inline_asm->outputs[index], "+A") &&
            !minic_riscv64_emit_sp_load64(
                file, minic_riscv64_inline_asm_registers[index], index * 8U)) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        size_t operand_index;

        operand_index = inline_asm->output_count + index;
        if (!minic_riscv64_emit_sp_load64(
                file, minic_riscv64_inline_asm_registers[operand_index], operand_index * 8U)) {
            return false;
        }
    }
    if (!emit_template(file, inline_asm)) {
        return false;
    }

    for (index = 0U; index < inline_asm->output_count; ++index) {
        const MinicInlineAsmOperand *operand;
        const MinicExpression *expression;

        operand = &inline_asm->outputs[index];
        if (!constraint_is(operand, "=r") && !constraint_is(operand, "=&r")) {
            continue;
        }
        expression = minic_c0_program_expression(program, operand->expression);
        if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
            !minic_riscv64_emit_object_store_register(file,
                                                      program,
                                                      function,
                                                      expression->value.local_id,
                                                      minic_riscv64_inline_asm_registers[index])) {
            return false;
        }
    }
    return minic_riscv64_emit_stack_release(file, temporary_size);
}
''')

# Linux-shaped LR/SC named-operand fixture.
fixture = root / "tests/compiler/c0/gnu_inline_asm_named_operands.c"
fixture.write_text(r'''typedef struct AtomicLike {
    int counter;
} AtomicLike;

static AtomicLike global_counter = {5};

static int fetch_add_unless_like(AtomicLike *v, int a, int u) {
    int prev;
    int rc;

    __asm__ __volatile__(
        "0:\tlr.w     %[p],  %[c]\n"
        "\tbeq      %[p],  %[u], 1f\n"
        "\tadd      %[rc], %[p], %[a]\n"
        "\tsc.w.rl  %[rc], %[rc], %[c]\n"
        "\tbnez     %[rc], 0b\n"
        "\tfence    rw, rw\n"
        "1:\n"
        : [p] "=&r"(prev), [rc] "=&r"(rc), [c] "+A"(v->counter)
        : [a] "r"(a), [u] "r"(u)
        : "memory");
    return prev;
}

int main(void) {
    int first;
    int second;

    first = fetch_add_unless_like(&global_counter, 3, 99);
    second = fetch_add_unless_like(&global_counter, 4, 8);
    return first == 5 && second == 8 && global_counter.counter == 8 ? 0 : 1;
}
''')

focused = root / "tests/compiler/c0/run-gnu-inline-asm-named-operands.sh"
focused.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-named-operands
assembly="$work/gnu_inline_asm_named_operands.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_named_operands.c" \
    -o "$work/gnu_inline_asm_named_operands.i"
"$minic" -S "$work/gnu_inline_asm_named_operands.i" -o "$assembly"

test -s "$assembly"
grep -E 'lr\.w[[:space:]]+t0,[[:space:]]*\(t3\)' "$assembly" >/dev/null
grep -E 'beq[[:space:]]+t0,[[:space:]]*t5,[[:space:]]*1f' "$assembly" >/dev/null
grep -E 'add[[:space:]]+t1,[[:space:]]*t0,[[:space:]]*t4' "$assembly" >/dev/null
grep -E 'sc\.w\.rl[[:space:]]+t1,[[:space:]]*t1,[[:space:]]*\(t3\)' "$assembly" >/dev/null
if grep -F '%[' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected named GNU asm placeholder in emitted assembly' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_named_operands names=p,rc,c,a,u early-clobber==&r address=+A input=r target=RV64'
''')

runtime = root / "tests/compiler/c0/run-gnu-inline-asm-named-operands-rv64.sh"
runtime.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-named-operands-rv64
source="$root/tests/compiler/c0/gnu_inline_asm_named_operands.c"

rm -rf "$work"
mkdir -p "$work"
"$riscv_cc" -E -P -std=gnu11 -x c "$source" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/minic.s"
"$riscv_cc" -static "$work/minic.s" -o "$work/minic.elf"
"$riscv_cc" -static -std=gnu11 "$source" -o "$work/gcc.elf"

set +e
"$qemu" "$work/gcc.elf"
gcc_status=$?
"$qemu" "$work/minic.elf"
minic_status=$?
set -e
if test "$gcc_status" -ne 0 || test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' "FAIL compiler/c0/gnu_inline_asm_named_operands_rv64 gcc=$gcc_status minic=$minic_status" >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_named_operands_rv64 gcc=minic lrsc=fetch-add-unless names=5 early-clobber=1 qemu=1'
''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    "sh tests/compiler/c0/run-gnu-inline-asm-operands.sh\n",
    "sh tests/compiler/c0/run-gnu-inline-asm-operands.sh\n"
    "sh tests/compiler/c0/run-gnu-inline-asm-named-operands.sh\n",
    "asm-named-focused-gate",
)
path.write_text(text)

# Make the real runtime differential a permanent normal-PR regression, not a one-shot check.
path = root / ".github/workflows/lua-stack-abi-validation.yml"
text = path.read_text()
text = replace_once(
    text,
    '''          MINIC="$GITHUB_WORKSPACE/build/rv64-stack-abi-compiler/bin/minic" \\\n          BUILD_DIR="$GITHUB_WORKSPACE/build/rv64-stack-abi" \\\n          RISCV_CC=riscv64-linux-gnu-gcc \\\n          QEMU_RISCV64=qemu-riscv64 \\\n            sh tests/compiler/c0/run-stack-fixed-arguments-rv64.sh\n''',
    '''          MINIC="$GITHUB_WORKSPACE/build/rv64-stack-abi-compiler/bin/minic" \\\n          BUILD_DIR="$GITHUB_WORKSPACE/build/rv64-stack-abi" \\\n          RISCV_CC=riscv64-linux-gnu-gcc \\\n          QEMU_RISCV64=qemu-riscv64 \\\n            sh tests/compiler/c0/run-stack-fixed-arguments-rv64.sh\n\n      - name: Run GCC-MiniC named GNU asm operand differential\n        shell: bash\n        run: |\n          set -Eeuo pipefail\n          MINIC="$GITHUB_WORKSPACE/build/rv64-stack-abi-compiler/bin/minic" \\\n          BUILD_DIR="$GITHUB_WORKSPACE/build/rv64-named-asm" \\\n          RISCV_CC=riscv64-linux-gnu-gcc \\\n          QEMU_RISCV64=qemu-riscv64 \\\n            sh tests/compiler/c0/run-gnu-inline-asm-named-operands-rv64.sh\n''',
    "asm-named-runtime-regression",
)
path.write_text(text)
