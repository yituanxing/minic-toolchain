#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M161 materializer {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


replace_once(
    "        function->value_count > (SIZE_MAX - frame->value_base_offset) / 8U) {\n",
    "        function->value_count > (SIZE_MAX - frame->value_base_offset) / 16U) {\n",
    "frame-value-capacity",
)
replace_once(
    "    storage_size = frame->value_base_offset + function->value_count * 8U;\n",
    "    /* M161_CORE_RV64_INT128_PAIR: one O0 spill slot can hold the largest\n"
    "       current Core scalar. i128 uses low64 at +0/high64 at +8. */\n"
    "    storage_size = frame->value_base_offset + function->value_count * 16U;\n",
    "frame-value-size",
)

replace_once(
    "        (size_t)value_id > (SIZE_MAX - frame->value_base_offset) / 8U) {\n",
    "        (size_t)value_id > (SIZE_MAX - frame->value_base_offset) / 16U) {\n",
    "value-offset-capacity",
)
replace_once(
    "    *offset = frame->value_base_offset + (size_t)value_id * 8U;\n",
    "    *offset = frame->value_base_offset + (size_t)value_id * 16U;\n",
    "value-offset-stride",
)

store_anchor = '''static bool store_core_value(FILE *file,
                             const MinicRiscv64CoreFrame *frame,
                             MinicCoreValueId value_id,
                             const char *register_name) {
    size_t offset;

    return core_value_offset(frame, value_id, &offset) &&
           minic_riscv64_emit_sp_store64(file, register_name, offset);
}
'''
helpers = store_anchor + '''
/* M161_CORE_RV64_INT128_PAIR: Core remains target-neutral; RV64 lowers wide
   integer values to a low/high XLEN pair only inside the target backend. */
static bool load_core_int128_value(FILE *file,
                                   const MinicRiscv64CoreFrame *frame,
                                   MinicCoreValueId value_id,
                                   const char *low_register,
                                   const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           core_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           minic_riscv64_emit_sp_load64(file, low_register, offset) &&
           minic_riscv64_emit_sp_load64(file, high_register, offset + 8U);
}

static bool store_core_int128_value(FILE *file,
                                    const MinicRiscv64CoreFrame *frame,
                                    MinicCoreValueId value_id,
                                    const char *low_register,
                                    const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           core_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           minic_riscv64_emit_sp_store64(file, low_register, offset) &&
           minic_riscv64_emit_sp_store64(file, high_register, offset + 8U);
}

static bool core_integer_type_is_signed(const MinicC0Program *program,
                                        MinicType type,
                                        bool *is_signed) {
    MinicType effective_type;

    if (program == NULL || is_signed == NULL || !minic_type_is_integer(type) ||
        !minic_c0_type_effective_integer_type(program, type, &effective_type)) {
        return false;
    }
    *is_signed = minic_type_is_signed_integer(effective_type);
    return true;
}
'''
replace_once(store_anchor, helpers, "int128-helpers")

old_mul = '''    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  mul t0, t0, t1\\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
new_mul = '''    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (minic_type_is_int128_integer(instruction->type)) {
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_int128_integer(function->values[right].type) ||
                !load_core_int128_value(file, frame, left, "t0", "t1") ||
                !load_core_int128_value(file, frame, right, "t2", "t3") ||
                fprintf(file,
                        "  mulhu t4, t0, t2\\n"
                        "  mul t5, t1, t2\\n"
                        "  add t4, t4, t5\\n"
                        "  mul t5, t0, t3\\n"
                        "  add t4, t4, t5\\n"
                        "  mul t0, t0, t2\\n") < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t4");
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  mul t0, t0, t1\\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
replace_once(old_mul, new_mul, "int128-multiply")

old_shift = '''    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_c0_type_effective_integer_type(program, instruction->type, &effective_type)) {
            return false;
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "srl" : "sra";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
'''
new_shift = '''    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_c0_type_effective_integer_type(program, instruction->type, &effective_type)) {
            return false;
        }
        if (minic_type_is_int128_integer(instruction->type)) {
            bool is_signed;
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_integer(function->values[right].type) ||
                minic_type_is_int128_integer(function->values[right].type) ||
                !core_integer_type_is_signed(program, instruction->type, &is_signed) ||
                !load_core_int128_value(file, frame, left, "t0", "t1") ||
                !load_core_value(file, frame, right, "t2") ||
                fprintf(file,
                        "  beqz t2, .L%s_core_i128_shr_done_%" PRIu32 "\\n"
                        "  li t3, 64\\n"
                        "  bgeu t2, t3, .L%s_core_i128_shr_ge64_%" PRIu32 "\\n"
                        "  neg t3, t2\\n"
                        "  sll t4, t1, t3\\n"
                        "  srl t0, t0, t2\\n"
                        "  or t0, t0, t4\\n"
                        "  %s t1, t1, t2\\n"
                        "  j .L%s_core_i128_shr_done_%" PRIu32 "\\n"
                        ".L%s_core_i128_shr_ge64_%" PRIu32 ":\\n"
                        "  addi t2, t2, -64\\n"
                        "  %s t0, t1, t2\\n"
                        "  %s\\n"
                        ".L%s_core_i128_shr_done_%" PRIu32 ":\\n",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        is_signed ? "  srai t1, t1, 63" : "  li t1, 0",
                        symbol_name,
                        instruction->result) < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "srl" : "sra";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
'''
replace_once(old_shift, new_shift, "int128-shift-right")

old_conversion = '''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
'''
new_conversion = '''    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;

        if (operand >= function->value_count) {
            return false;
        }
        source_type = function->values[operand].type;
        if (minic_type_is_int128_integer(instruction->type)) {
            if (minic_type_is_int128_integer(source_type)) {
                if (!load_core_int128_value(file, frame, operand, "t0", "t1")) {
                    return false;
                }
            } else {
                bool source_signed;

                if (!minic_type_is_integer(source_type) ||
                    !load_core_value(file, frame, operand, "t0") ||
                    !core_integer_type_is_signed(program, source_type, &source_signed) ||
                    fprintf(file,
                            source_signed ? "  srai t1, t0, 63\\n" : "  li t1, 0\\n") < 0) {
                    return false;
                }
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_type_is_int128_integer(source_type)) {
            if (!load_core_int128_value(file, frame, operand, "t0", "t1") ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, instruction->type, "t0")) {
                return false;
            }
            return store_core_value(file, frame, instruction->result, "t0");
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
'''
replace_once(old_conversion, new_conversion, "int128-conversion")

old_load = '''    case MINIC_CORE_INSTRUCTION_LOAD:
        if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load_for_program(
                file, program, instruction->type, "t1", "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
'''
new_load = '''    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_int128_integer(instruction->type)) {
            if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
                fprintf(file, "  ld t1, 0(t0)\\n  ld t2, 8(t0)\\n") < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t1", "t2");
        }
        if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load_for_program(
                file, program, instruction->type, "t1", "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
'''
replace_once(old_load, new_load, "int128-load-object")

old_store = '''    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        return load_core_value(file, frame, instruction->value.store.address, "t0") &&
               load_core_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
'''
new_store = '''    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        if (minic_type_is_int128_integer(stored_type)) {
            return load_core_value(file, frame, instruction->value.store.address, "t0") &&
                   load_core_int128_value(file, frame, stored_value, "t1", "t2") &&
                   fprintf(file, "  sd t1, 0(t0)\\n  sd t2, 8(t0)\\n") >= 0;
        }
        return load_core_value(file, frame, instruction->value.store.address, "t0") &&
               load_core_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
'''
replace_once(old_store, new_store, "int128-store-object")

PATH.write_text(text)
print("M161_INT128_PAIR_APPLIED")
