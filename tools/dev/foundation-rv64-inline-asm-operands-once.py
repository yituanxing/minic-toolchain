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

# AST: operand access is generic now that inputs are first-class.
path = root / "src/frontend/ast.h"
text = path.read_text()
text = replace_once(
    text,
    '''typedef enum MinicInlineAsmOutputAccess {\n    MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY = 0,\n    MINIC_INLINE_ASM_OUTPUT_READ_WRITE\n} MinicInlineAsmOutputAccess;\n\ntypedef struct MinicInlineAsmOperand {\n    char *constraint_text;\n    size_t constraint_length;\n    MinicExpressionId expression;\n    MinicInlineAsmOutputAccess output_access;\n} MinicInlineAsmOperand;\n''',
    '''typedef enum MinicInlineAsmOperandAccess {\n    MINIC_INLINE_ASM_OPERAND_READ_ONLY = 0,\n    MINIC_INLINE_ASM_OPERAND_WRITE_ONLY,\n    MINIC_INLINE_ASM_OPERAND_READ_WRITE\n} MinicInlineAsmOperandAccess;\n\ntypedef struct MinicInlineAsmOperand {\n    char *constraint_text;\n    size_t constraint_length;\n    MinicExpressionId expression;\n    MinicInlineAsmOperandAccess access;\n} MinicInlineAsmOperand;\n''',
    "asm-operand-access-model",
)
text = replace_once(
    text,
    '''bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,\n                                            MinicInlineAsmId inline_asm_id,\n                                            const char *constraint_text,\n                                            size_t constraint_length,\n                                            MinicExpressionId expression,\n                                            MinicInlineAsmOutputAccess output_access);\n''',
    '''bool minic_c0_program_add_inline_asm_output(MinicC0Program *program,\n                                            MinicInlineAsmId inline_asm_id,\n                                            const char *constraint_text,\n                                            size_t constraint_length,\n                                            MinicExpressionId expression,\n                                            MinicInlineAsmOperandAccess access);\nbool minic_c0_program_add_inline_asm_input(MinicC0Program *program,\n                                           MinicInlineAsmId inline_asm_id,\n                                           const char *constraint_text,\n                                           size_t constraint_length,\n                                           MinicExpressionId expression);\n''',
    "asm-input-api-contract",
)
path.write_text(text)

# Program ownership/API: outputs and inputs share the operand record.
path = root / "src/frontend/ast.c"
text = path.read_text()
text = text.replace("MinicInlineAsmOutputAccess output_access", "MinicInlineAsmOperandAccess access")
text = text.replace("output_access != MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY", "access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY")
text = text.replace("output_access != MINIC_INLINE_ASM_OUTPUT_READ_WRITE", "access != MINIC_INLINE_ASM_OPERAND_READ_WRITE")
text = text.replace("operand.output_access = output_access;", "operand.access = access;")
anchor = '''bool minic_c0_program_set_inline_asm_memory_clobber(MinicC0Program *program,\n                                                    MinicInlineAsmId inline_asm_id,\n                                                    bool has_memory_clobber) {\n'''
input_api = r'''bool minic_c0_program_add_inline_asm_input(MinicC0Program *program,
                                           MinicInlineAsmId inline_asm_id,
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
    if (!minic_grow_array((void **)&inline_asm->inputs,
                          &inline_asm->input_capacity,
                          inline_asm->input_count,
                          sizeof(*inline_asm->inputs))) {
        return false;
    }
    (void)memset(&operand, 0, sizeof(operand));
    operand.constraint_text = minic_copy_name(constraint_text, constraint_length);
    if (operand.constraint_text == NULL) {
        return false;
    }
    operand.constraint_length = constraint_length;
    operand.expression = expression;
    operand.access = MINIC_INLINE_ASM_OPERAND_READ_ONLY;
    inline_asm->inputs[inline_asm->input_count] = operand;
    inline_asm->input_count += 1U;
    return true;
}

'''
text = replace_once(text, anchor, input_api + anchor, "asm-input-api-implementation")
path.write_text(text)

# Parser: collect input operands with raw constraints; target interprets letters.
path = root / "src/frontend/parser_statement.c"
text = path.read_text()
text = text.replace("MinicInlineAsmOutputAccess access;", "MinicInlineAsmOperandAccess access;")
text = text.replace("MINIC_INLINE_ASM_OUTPUT_READ_WRITE", "MINIC_INLINE_ASM_OPERAND_READ_WRITE")
text = text.replace("MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY", "MINIC_INLINE_ASM_OPERAND_WRITE_ONLY")
anchor = '''static bool parse_gnu_inline_asm_statement(MinicParser *parser) {\n'''
input_parser = r'''static bool parse_gnu_inline_asm_input(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    const MinicExpression *operand_expression;
    MinicExpressionId operand_id;
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
    if (constraint_length == 0U || constraint[0] == '+' || constraint[0] == '=') {
        free(constraint);
        minic_parser_error(parser, "GNU asm input constraint must describe a read-only operand");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before GNU asm input expression") ||
        !minic_parser_parse_expression(parser, &operand_id, 0U) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm input expression")) {
        free(constraint);
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand_id);
    if (operand_expression == NULL ||
        (!minic_type_is_integer(operand_expression->type) &&
         !minic_type_is_pointer(operand_expression->type))) {
        free(constraint);
        minic_parser_error(parser, "GNU asm input operand currently requires an integer or pointer");
        return false;
    }
    if (!minic_c0_program_add_inline_asm_input(
            parser->program, inline_asm_id, constraint, constraint_length, operand_id)) {
        free(constraint);
        minic_parser_error(parser, "cannot store GNU asm input operand");
        return false;
    }
    free(constraint);
    return true;
}

'''
text = replace_once(text, anchor, input_parser + anchor, "asm-input-parser")
old_inputs = r'''            if (parser->current.kind != MINIC_TOKEN_COLON &&
                parser->current.kind != MINIC_TOKEN_RPAREN) {
                minic_parser_error(parser, "GNU asm input operands are not supported yet");
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_COLON) {
'''
new_inputs = r'''            while (parser->current.kind != MINIC_TOKEN_COLON &&
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
            if (parser->current.kind == MINIC_TOKEN_COLON) {
'''
text = replace_once(text, old_inputs, new_inputs, "asm-input-list-parser")
path.write_text(text)

# Verifier: frontend validates ownership/access, not RV64 constraint letters.
path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
text = replace_once(text, "            inline_asm->input_count != 0U || inline_asm->clobber_count > 1U ||\n", "            inline_asm->clobber_count > 1U ||\n", "asm-verifier-enable-inputs")
text = text.replace("operand->output_access != MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY", "operand->access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY")
text = text.replace("operand->output_access != MINIC_INLINE_ASM_OUTPUT_READ_WRITE", "operand->access != MINIC_INLINE_ASM_OPERAND_READ_WRITE")
old_return = '''        }\n        return true;\n    }\n\n    case MINIC_STATEMENT_RETURN:\n'''
new_return = r'''        }
        for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;

            operand = &inline_asm->inputs[operand_index];
            operand_expression = minic_c0_program_expression(program, operand->expression);
            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||
                operand_expression == NULL || operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
                return false;
            }
        }
        return true;
    }

    case MINIC_STATEMENT_RETURN:
'''
# Use the occurrence following the inline-asm output loop.
pos = text.find("    case MINIC_STATEMENT_INLINE_ASM: {")
if pos < 0:
    raise SystemExit("asm-verifier-input-loop: inline asm case not found")
end = text.find(old_return, pos)
if end < 0:
    raise SystemExit("asm-verifier-input-loop: return anchor not found")
text = text[:end] + new_return + text[end + len(old_return):]
path.write_text(text)

# Target API + new dedicated lowering owner.
path = root / "src/target/riscv64/codegen_internal.h"
text = path.read_text()
text = replace_once(
    text,
    '''bool minic_riscv64_emit_expression(FILE *file,\n                                   const MinicC0Program *program,\n                                   const MinicFunction *function,\n                                   MinicExpressionId expression_id);\n''',
    '''bool minic_riscv64_emit_expression(FILE *file,\n                                   const MinicC0Program *program,\n                                   const MinicFunction *function,\n                                   MinicExpressionId expression_id);\nbool minic_riscv64_emit_inline_asm(FILE *file,\n                                    const MinicC0Program *program,\n                                    const MinicFunction *function,\n                                    const MinicStatement *statement);\n''',
    "asm-target-prototype",
)
path.write_text(text)

inline_asm_c = r'''#include "target/riscv64/codegen_internal.h"

#include <stdint.h>
#include <string.h>

#define MINIC_RISCV64_INLINE_ASM_MAX_OPERANDS 6U

static const char *const minic_riscv64_inline_asm_registers[] = {
    "t0", "t1", "t3", "t4", "t5", "t6"
};

static bool constraint_is(const MinicInlineAsmOperand *operand, const char *text) {
    size_t length;

    if (operand == NULL || text == NULL || operand->constraint_text == NULL) {
        return false;
    }
    length = strlen(text);
    return operand->constraint_length == length &&
           memcmp(operand->constraint_text, text, length) == 0;
}

static bool validate_output(const MinicC0Program *program,
                            const MinicInlineAsmOperand *operand) {
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
    if (constraint_is(operand, "=r")) {
        return operand->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
               expression->kind == MINIC_EXPRESSION_LOCAL &&
               (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
    }
    return false;
}

static bool validate_input(const MinicC0Program *program, const MinicInlineAsmOperand *operand) {
    const MinicExpression *expression;

    if (program == NULL || operand == NULL || operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
        !constraint_is(operand, "r")) {
        return false;
    }
    expression = minic_c0_program_expression(program, operand->expression);
    return expression != NULL &&
           (minic_type_is_integer(expression->type) || minic_type_is_pointer(expression->type));
}

static bool template_operands_are_valid(const MinicInlineAsm *inline_asm, size_t operand_count) {
    size_t index;

    if (inline_asm == NULL || inline_asm->template_text == NULL) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;

        ch = (unsigned char)inline_asm->template_text[index];
        if (ch != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length) {
            return false;
        }
        index += 1U;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            continue;
        }
        if (ch < '0' || ch > '9' || (size_t)(ch - '0') >= operand_count) {
            return false;
        }
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
        unsigned char ch;

        ch = (unsigned char)inline_asm->template_text[index];
        if (ch != '%') {
            if (fputc((int)ch, file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        {
            size_t operand_index;
            const MinicInlineAsmOperand *operand;
            const char *register_name;

            operand_index = (size_t)(ch - '0');
            register_name = minic_riscv64_inline_asm_registers[operand_index];
            if (operand_index < inline_asm->output_count) {
                operand = &inline_asm->outputs[operand_index];
                if (constraint_is(operand, "+A")) {
                    if (fprintf(file, "(%s)", register_name) < 0) {
                        return false;
                    }
                    continue;
                }
            }
            if (fputs(register_name, file) == EOF) {
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
        if (!minic_riscv64_emit_sp_load64(file,
                                          minic_riscv64_inline_asm_registers[operand_index],
                                          operand_index * 8U)) {
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
        if (!constraint_is(operand, "=r")) {
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
'''
(root / "src/target/riscv64/codegen_inline_asm.c").write_text(inline_asm_c)

# Statement codegen delegates target-specific constraints to the new owner.
path = root / "src/target/riscv64/codegen_statement.c"
text = path.read_text()
text = replace_range(
    text,
    "    case MINIC_STATEMENT_INLINE_ASM: {",
    "\n    case MINIC_STATEMENT_RETURN:",
    "    case MINIC_STATEMENT_INLINE_ASM:\n        return minic_riscv64_emit_inline_asm(file, program, function, statement);\n",
    "asm-statement-delegation",
)
path.write_text(text)

# Build includes the dedicated target module.
path = root / "Makefile"
text = path.read_text()
text = replace_once(
    text,
    "\tsrc/target/riscv64/codegen_expression.c \\\n\tsrc/target/riscv64/codegen_statement.c \\\n",
    "\tsrc/target/riscv64/codegen_expression.c \\\n\tsrc/target/riscv64/codegen_inline_asm.c \\\n\tsrc/target/riscv64/codegen_statement.c \\\n",
    "asm-target-build-source",
)
path.write_text(text)

# Focused source: cover the current Linux 3030 and immediate 3036 shapes.
fixture = root / "tests/compiler/c0/gnu_inline_asm_operands.c"
fixture.write_text(r'''typedef struct AtomicLike {
    int counter;
} AtomicLike;

static AtomicLike global_counter = {7};

static void atomic_add_like(int value, AtomicLike *target) {
    __asm__ __volatile__("amoadd.w zero, %1, %0"
                         : "+A"(target->counter)
                         : "r"(value)
                         : "memory");
}

static int atomic_fetch_add_like(int value, AtomicLike *target) {
    register int previous;

    __asm__ __volatile__("amoadd.w %1, %2, %0"
                         : "+A"(target->counter), "=r"(previous)
                         : "r"(value)
                         : "memory");
    return previous;
}

int main(void) {
    int previous;

    atomic_add_like(5, &global_counter);
    previous = atomic_fetch_add_like(3, &global_counter);
    return previous == 12 && global_counter.counter == 15 ? 0 : 1;
}
''')

focused = root / "tests/compiler/c0/run-gnu-inline-asm-operands.sh"
focused.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-operands
assembly="$work/gnu_inline_asm_operands.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_operands.c" \
    -o "$work/gnu_inline_asm_operands.i"
"$minic" -S "$work/gnu_inline_asm_operands.i" -o "$assembly"

test -s "$assembly"
grep -F 'amoadd.w zero, t1, (t0)' "$assembly" >/dev/null
grep -F 'amoadd.w t1, t3, (t0)' "$assembly" >/dev/null
if grep -E '\+A|"r"|=r' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected raw GNU asm constraints in emitted assembly' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_operands outputs=+A,=r input=r placeholders=0,1,2 staging=stack target=RV64'
''')

runtime = root / "tests/compiler/c0/run-gnu-inline-asm-operands-rv64.sh"
runtime.write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-operands-rv64
source="$root/tests/compiler/c0/gnu_inline_asm_operands.c"

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
    printf '%s\n' "FAIL compiler/c0/gnu_inline_asm_operands_rv64 gcc=$gcc_status minic=$minic_status" >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_operands_rv64 gcc=minic amoadd=write+fetch constraints=+A,=r,r qemu=1'
''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    "sh tests/compiler/c0/run-gnu-inline-asm-readwrite-output.sh\n",
    "sh tests/compiler/c0/run-gnu-inline-asm-readwrite-output.sh\n"
    "sh tests/compiler/c0/run-gnu-inline-asm-operands.sh\n",
    "asm-focused-gate",
)
path.write_text(text)
