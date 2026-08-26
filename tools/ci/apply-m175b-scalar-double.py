#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: anchor count={count}")
    return text.replace(old, new, 1)


def replace_nth(text: str, old: str, new: str, occurrence: int, label: str) -> str:
    start = -1
    for _ in range(occurrence):
        start = text.find(old, start + 1)
        if start < 0:
            raise SystemExit(f"{label}: occurrence {occurrence} not found")
    return text[:start] + new + text[start + len(old):]


# Core IR vocabulary: keep floating support deliberately narrow.  Double is
# transported as a typed SSA value whose payload is raw IEEE-754 bits; only the
# two C conversion directions needed by the Core-default fast gate are semantic
# conversion instructions here.
path = Path("src/core/core_ir.h")
text = path.read_text()
text = replace_once(
    text,
    "    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,\n",
    "    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT = 0,\n"
    "    /* M175B_SCALAR_DOUBLE_BRIDGE: raw IEEE-754 double payload. */\n"
    "    MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT,\n",
    "core-ir floating constant enum",
)
text = replace_once(
    text,
    "    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n",
    "    MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION,\n"
    "    MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE,\n"
    "    MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER,\n",
    "core-ir conversion enums",
)
text = replace_once(
    text,
    "        int64_t integer_value;\n",
    "        int64_t integer_value;\n"
    "        uint64_t floating_bits;\n",
    "core-ir floating payload",
)
path.write_text(text)


# Core verifier/dump ownership.
path = Path("src/core/core_ir.c")
text = path.read_text()
text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type);\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type);\n"
    "    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_double(instruction->type);\n",
    "core-ir floating constant verifier",
)
text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_integer(function->values[instruction->value.operand].type);\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_integer(function->values[instruction->value.operand].type);\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_double(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_integer(function->values[instruction->value.operand].type);\n"
    "    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:\n"
    "        return instruction_result_is_valid(function, instruction) &&\n"
    "               minic_type_is_integer(instruction->type) &&\n"
    "               instruction->value.operand < function->value_count &&\n"
    "               available_values[instruction->value.operand] &&\n"
    "               minic_type_is_double(function->values[instruction->value.operand].type);\n",
    "core-ir floating conversion verifier",
)
text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = const.int %\" PRId64 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.integer_value) >= 0;\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = const.int %\" PRId64 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.integer_value) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = const.double.bits 0x%016\" PRIx64 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.floating_bits) >= 0;\n",
    "core-ir floating constant dump",
)
text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = convert.int %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = convert.int %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = convert.int-to-double %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n"
    "    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:\n"
    "        return fprintf(output,\n"
    "                       \"  %%%\" PRIu32 \" = convert.double-to-int %%%\" PRIu32 \"\\n\",\n"
    "                       instruction->result,\n"
    "                       instruction->value.operand) >= 0;\n",
    "core-ir floating conversion dump",
)
path.write_text(text)


# Semantic AST -> Core lowering.  Do not add floating arithmetic, calls, or
# conditions here; this milestone owns only scalar storage plus int<->double.
path = Path("src/core/core_lower.c")
text = path.read_text()
text = replace_once(
    text,
    "static bool core_memory_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n"
    "}\n",
    "static bool core_memory_scalar_type(MinicType type) {\n"
    "    /* M175B_SCALAR_DOUBLE_BRIDGE: double participates in Core scalar\n"
    "       storage/value transport, without implying floating arithmetic. */\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||\n"
    "           minic_type_is_double(type);\n"
    "}\n",
    "core-lower scalar type",
)
conversion_start = text.find("    if (expression->kind == MINIC_EXPRESSION_CONVERSION) {\n")
conversion_end = text.find("    /* M79_CALL_FRAME_RETURN_ADDRESS", conversion_start)
if conversion_start < 0 or conversion_end < 0:
    raise SystemExit("core-lower conversion span not found")
old_conversion = text[conversion_start:conversion_end]
new_conversion = '''    if (expression->kind == MINIC_EXPRESSION_FLOATING) {
        if (!minic_type_is_double(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        instruction.value.floating_bits = expression->value.floating_bits;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand;
        MinicExpressionId operand_id;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType source_type;
        MinicType target_type;

        operand_id = expression->value.unary.operand;
        operand = minic_c0_program_expression(context->body->program, operand_id);
        if (operand == NULL ||
            !minic_type_unqualified(expression->type, &target_type) ||
            !core_scalar_expression_value_type(context->body, operand, &source_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, operand_id, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, source_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_integer(target_type) && minic_type_is_integer(source_type)) {
            return append_integer_conversion(
                context, expression->span, target_type, operand_value, value_id);
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.span = expression->span;
        instruction.type = target_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        if (minic_type_is_double(target_type) && minic_type_is_integer(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
        } else if (minic_type_is_integer(target_type) && minic_type_is_double(source_type)) {
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
text = text[:conversion_start] + new_conversion + text[conversion_end:]
path.write_text(text)


# Core -> RV64: double SSA values remain raw 64-bit bits in ordinary Core spill
# slots.  F registers are used only at semantic conversion/return boundaries.
path = Path("src/target/riscv64/core_codegen.c")
text = path.read_text()
text = replace_once(
    text,
    "static bool core_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n"
    "}\n",
    "static bool core_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||\n"
    "           minic_type_is_double(type);\n"
    "}\n\n"
    "static bool core_effective_integer_type(const MinicC0Program *program,\n"
    "                                        MinicType type,\n"
    "                                        MinicType *effective_type) {\n"
    "    if (effective_type == NULL || !minic_type_is_integer(type)) {\n"
    "        return false;\n"
    "    }\n"
    "    if (minic_type_is_enum(type)) {\n"
    "        return program != NULL &&\n"
    "               minic_c0_type_effective_integer_type(program, type, effective_type);\n"
    "    }\n"
    "    *effective_type = type;\n"
    "    return true;\n"
    "}\n",
    "core-codegen scalar type",
)
# Support-switch occurrences: add the new semantic instructions while leaving
# call ABI checks intact (floating-register arguments/results remain unsupported
# there in this milestone).
text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:\n"
    "    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:\n",
    "core-codegen support floating constant",
)
text = replace_once(
    text,
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
    "    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:\n"
    "    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:\n"
    "    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:\n",
    "core-codegen support conversions",
)
# The function-level ABI gate is the third VOID/INTEGER return-kind gate in
# this file (direct call, indirect call, then function). Permit a floating
# return only for the current function; call-result ownership stays fail-closed.
return_gate = (
    "        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&\n"
    "                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER) {\n"
    "            return false;\n"
    "        }\n"
)
return_gate_new = (
    "        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&\n"
    "                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&\n"
    "                   !(return_value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&\n"
    "                     minic_type_is_double(function->return_type))) {\n"
    "            return false;\n"
    "        }\n"
)
text = replace_nth(text, return_gate, return_gate_new, 1, "core-codegen function return ABI gate") if text.count(return_gate) == 1 else text
# On the current source the direct/indirect variants have different indentation;
# if the exact function gate still exists, replace the last matching occurrence.
if return_gate in text:
    count = text.count(return_gate)
    text = replace_nth(text, return_gate, return_gate_new, count, "core-codegen function return ABI gate fallback")

# Emitter: insert raw floating constant after integer constant.
integer_emit = '''    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (fprintf(file, "  li t0, %" PRId64 "\\n", instruction->value.integer_value) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
floating_emit = integer_emit + '''    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (!minic_type_is_double(instruction->type) ||
            fprintf(file, "  li t0, 0x%016" PRIx64 "\\n", instruction->value.floating_bits) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
# The first INTEGER_CONSTANT occurrence was support-switch; target the emitter
# by its full body rather than by enum token alone.
text = replace_once(text, integer_emit, floating_emit, "core-codegen floating constant emitter")

# Insert the two conversion emitters immediately before the emitter's scalar
# bitcast case (second SCALAR_BITCAST case in the file).
bitcast_case = "    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:\n"
conversion_emitters = '''    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !core_effective_integer_type(program, function->values[operand].type, &source_type) ||
            minic_type_is_int128_integer(source_type) ||
            !minic_type_is_double(instruction->type)) {
            return false;
        }
        if (minic_type_is_long_integer(source_type)) {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.lu" : "fcvt.d.l";
        } else {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.wu" : "fcvt.d.w";
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  %s ft0, t0\\n"
                    "  fmv.x.d t1, ft0\\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType target_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !core_effective_integer_type(program, instruction->type, &target_type) ||
            minic_type_is_int128_integer(target_type)) {
            return false;
        }
        if (minic_type_is_long_integer(target_type)) {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.lu.d" : "fcvt.l.d";
        } else {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.wu.d" : "fcvt.w.d";
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\\n"
                    "  %s t1, ft0, rtz\\n",
                    opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t1")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
'''
text = replace_nth(
    text,
    bitcast_case,
    conversion_emitters + bitcast_case,
    2,
    "core-codegen conversion emitters",
)

return_old = '''        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !load_core_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return fprintf(file, "  j .L%s_core_return\\n", symbol_name) >= 0;
'''
return_new = '''        } else if (minic_type_is_double(function->return_type)) {
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !load_core_value(file, frame, terminator->return_value, "t0") ||
                fprintf(file, "  fmv.d.x fa0, t0\\n") < 0) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !load_core_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return fprintf(file, "  j .L%s_core_return\\n", symbol_name) >= 0;
'''
text = replace_once(text, return_old, return_new, "core-codegen double return")
path.write_text(text)
