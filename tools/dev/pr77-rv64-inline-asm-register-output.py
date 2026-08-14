#!/usr/bin/env python3
from pathlib import Path

# Frontend owns raw template/constraints; target lowering owns placeholder and
# register-constraint interpretation. Remove the temporary parser-wide '%' ban.
path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = r'''    if (strchr(template_text, '%') != NULL) {
        free(template_text);
        minic_parser_error(parser,
                           "GNU asm operand substitutions require target template support");
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"inline asm template handoff: expected one parser percent guard, found {text.count(old)}")
path.write_text(text.replace(old, "", 1))

# Replace the staged RV64 InlineAsm statement lowering structurally. Current
# target capability has three truthful shapes:
#   * no outputs: literal template (fence path)
#   * one +rm output + empty template: compiler barrier/dataflow edge
#   * one =r output: bind %0 to t0, emit template, then store t0 to the local
# Unsupported placeholders/constraints remain rejected at the target boundary.
path = Path("src/target/riscv64/codegen_statement.c")
text = path.read_text()
start = text.find("    case MINIC_STATEMENT_INLINE_ASM: {")
end = text.find("\n    case MINIC_STATEMENT_RETURN:", start)
if start < 0 or end < 0:
    raise SystemExit("RV64 =r asm: cannot locate staged inline-asm codegen case")
replacement = r'''    case MINIC_STATEMENT_INLINE_ASM: {
        const MinicInlineAsm *inline_asm;

        inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
        if (inline_asm == NULL || inline_asm->template_text == NULL ||
            inline_asm->input_count != 0U) {
            return false;
        }
        if (inline_asm->output_count == 0U) {
            return fprintf(file, "  %s\n", inline_asm->template_text) >= 0;
        }
        if (inline_asm->output_count == 1U) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;

            operand = &inline_asm->outputs[0];
            operand_expression = minic_c0_program_expression(program, operand->expression);
            if (operand_expression == NULL || operand_expression->kind != MINIC_EXPRESSION_LOCAL ||
                operand_expression->value_category != MINIC_VALUE_LVALUE) {
                return false;
            }
            if (operand->output_access == MINIC_INLINE_ASM_OUTPUT_READ_WRITE &&
                operand->constraint_length == 3U &&
                memcmp(operand->constraint_text, "+rm", 3U) == 0) {
                return inline_asm->template_length == 0U;
            }
            if (operand->output_access == MINIC_INLINE_ASM_OUTPUT_WRITE_ONLY &&
                operand->constraint_length == 2U &&
                memcmp(operand->constraint_text, "=r", 2U) == 0) {
                size_t index;

                if (fprintf(file, "  ") < 0) {
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
                    if (index + 1U >= inline_asm->template_length) {
                        return false;
                    }
                    index += 1U;
                    ch = (unsigned char)inline_asm->template_text[index];
                    if (ch == '0') {
                        if (fputs("t0", file) == EOF) {
                            return false;
                        }
                    } else if (ch == '%') {
                        if (fputc('%', file) == EOF) {
                            return false;
                        }
                    } else {
                        return false;
                    }
                }
                return fputc('\n', file) != EOF &&
                       minic_riscv64_emit_object_store_register(
                           file,
                           program,
                           function,
                           operand_expression->value.local_id,
                           "t0");
            }
        }
        return false;
    }
'''
path.write_text(text[:start] + replacement + text[end:])
print("staged RV64 GNU asm =r output with %0 target substitution and local writeback")
