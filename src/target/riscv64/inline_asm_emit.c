#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv/typed_memory.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/isa.h"
#include "target/common/lp64_scalar.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const MinicCoreType *rv64_asm_core_base(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    if (function == NULL || function->type_context == NULL) return NULL;
    return minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context, id));
}

static bool rv64_asm_core_pointer(const MinicCoreFunction *function,
                                  MinicCoreTypeId id) {
    const MinicCoreType *pointer = rv64_asm_core_base(function, id);
    return pointer != NULL && pointer->kind == MINIC_CORE_TYPE_POINTER &&
           pointer->has_layout && pointer->size == 8U;
}

static bool rv64_asm_core_scalar(const MinicCoreFunction *function,
                                 MinicCoreTypeId id) {
    const MinicCoreType *type = rv64_asm_core_base(function, id);
    if (type == NULL || !type->has_layout) return false;
    if (type->kind == MINIC_CORE_TYPE_POINTER)
        return rv64_asm_core_pointer(function, id);
    if (type->kind == MINIC_CORE_TYPE_INTEGER)
        return (type->size == 1U || type->size == 2U ||
                type->size == 4U || type->size == 8U) &&
               type->value.integer.bit_width == type->size * 8U &&
               (!type->value.integer.is_bool || type->size == 1U);
    if (type->kind == MINIC_CORE_TYPE_FLOAT)
        return (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 &&
                type->size == 4U) ||
               (type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 &&
                type->size == 8U);
    return false;
}

static bool rv64_asm_core_pointer_scalar(const MinicCoreFunction *function,
                                         MinicCoreTypeId pointer_id,
                                         MinicCoreTypeId *pointee_id) {
    return rv64_asm_core_pointer(function, pointer_id) &&
        minic_lp64_core_pointer_pointee(function->type_context, pointer_id,
                                        pointee_id) &&
        rv64_asm_core_scalar(function, *pointee_id);
}

static bool rv64_asm_void(const MinicCoreFunction *function,
                          MinicCoreTypeId id) {
    const MinicCoreType *type;
    type = rv64_asm_core_base(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_VOID;
}

static bool rv64_asm_integer_or_pointer(const MinicCoreFunction *function,
                                        MinicCoreTypeId id) {
    const MinicCoreType *type;
    if (!rv64_asm_core_scalar(function, id)) return false;
    type = rv64_asm_core_base(function, id);
    return type->kind == MINIC_CORE_TYPE_INTEGER ||
           type->kind == MINIC_CORE_TYPE_POINTER;
}

static bool rv64_asm_normalize_integer(FILE *file,
                                       const MinicCoreFunction *function,
                                       MinicCoreTypeId type_id,
                                       const char *register_name) {
    const MinicCoreType *type = rv64_asm_core_base(function, type_id);
    return type != NULL &&
        (type->kind != MINIC_CORE_TYPE_INTEGER ||
         minic_riscv64_emit_core_integer_conversion(
             file, function->type_context, type_id, register_name));
}

static bool rv64_asm_emit_scalar_load(FILE *file,
                                      const MinicCoreFunction *function,
                                      MinicCoreTypeId type_id,
                                      const char *destination, const char *address,
                                      const char *scratch, size_t alignment) {
    return function != NULL && function->type_context != NULL &&
        minic_riscv64_emit_safe_core_scalar_load(file, function->type_context,
            type_id, destination, address, scratch, alignment);
}

static bool rv64_asm_emit_scalar_store(FILE *file,
                                       const MinicCoreFunction *function,
                                       MinicCoreTypeId type_id,
                                       const char *source_register, const char *address,
                                       const char *scratch, size_t alignment) {
    return function != NULL && function->type_context != NULL &&
        minic_riscv64_emit_safe_core_scalar_store(file, function->type_context,
            type_id, source_register, address, scratch, alignment);
}

/* One owner for RV64 Core inline-asm checks and the matching machine forms.
 * Physical register allocation is owned by asm_plan and consumed here. */
const MinicCoreFixedRegisterBinding *minic_riscv64_inline_asm_fixed_register_binding(
    const MinicCoreFunction *function, size_t binding_id) {
    return function != NULL && binding_id < function->fixed_register_binding_count
               ? &function->fixed_register_bindings[binding_id]
               : NULL;
}

static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,
                                             const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: opaque volatile asm may carry zero
       target bytes; emit_opaque_inline_asm naturally loops zero times. */
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
        return false;
    }
    /* M76_SINGLE_LABEL_ASM_GOTO: the target is explicit Core metadata even
       though the target-specific template remains opaque. */
    return !inline_asm->is_goto || inline_asm->goto_target < function->block_count;
}

static bool core_register_output_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM ||
        !rv64_asm_integer_or_pointer(function, instruction->core_type_id) ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_register_output_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId operand;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM ||
        !rv64_asm_integer_or_pointer(function, instruction->core_type_id) ||
        instruction->value.register_output_input_inline_asm.inline_asm_id >=
            function->inline_asm_count) {
        return false;
    }
    operand = instruction->value.register_output_input_inline_asm.operand;
    if (operand >= function->value_count ||
        !rv64_asm_integer_or_pointer(function,
            function->values[operand].core_type_id)) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.register_output_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0' &&
             inline_asm->template_text[index + 1U] != '1')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_memory_readwrite_scalar_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId memory_address;
    MinicCoreValueId operand;
    size_t memory_index;
    size_t register_index;
    size_t scalar_index;
    size_t index;
    bool has_register_output;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM ||
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id >=
            function->inline_asm_count) {
        return false;
    }
    memory_address = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address;
    operand = instruction->value.memory_readwrite_scalar_input_inline_asm.operand;
    memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
    register_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
    scalar_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
    has_register_output = register_index != SIZE_MAX;
    if (memory_address >= function->value_count || operand >= function->value_count ||
        !rv64_asm_core_pointer(function,
            function->values[memory_address].core_type_id) ||
        !rv64_asm_integer_or_pointer(function,
            function->values[operand].core_type_id) ||
        memory_index >= MINIC_RISCV64_ASM_MAX_OPERANDS ||
        scalar_index >= MINIC_RISCV64_ASM_MAX_OPERANDS ||
        memory_index == scalar_index ||
        (has_register_output &&
         (register_index >= MINIC_RISCV64_ASM_MAX_OPERANDS ||
          register_index == memory_index || register_index == scalar_index))) {
        return false;
    }
    if (has_register_output) {
        if (!rv64_asm_integer_or_pointer(function, instruction->core_type_id) ||
            instruction->result == MINIC_CORE_VALUE_INVALID) {
            return false;
        }
    } else if (!rv64_asm_void(function, instruction->core_type_id) ||
               instruction->result != MINIC_CORE_VALUE_INVALID) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || !inline_asm->has_memory_clobber) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index + 1U];
        if (ch == '%') {
            index += 1U;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_index != memory_index && operand_index != scalar_index &&
            (!has_register_output || operand_index != register_index)) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_scalar_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId operand;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !rv64_asm_void(function, instruction->core_type_id) ||
        instruction->value.scalar_input_inline_asm.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    operand = instruction->value.scalar_input_inline_asm.operand;
    if (operand >= function->value_count ||
        !rv64_asm_integer_or_pointer(function,
            function->values[operand].core_type_id)) {
        return false;
    }
    inline_asm =
        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

/* M126A_GENERIC_STRUCTURED_ASM: capability is now role/resource based. */
static bool core_structured_inline_asm_supported(const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicRiscv64AsmInstructionPlan probe;
    bool bound[MINIC_RISCV64_ASM_MAX_OPERANDS] = {false};
    size_t binding_index;
    size_t template_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !rv64_asm_void(function, instruction->core_type_id) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count == 0U ||
        instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile || inline_asm->is_goto) {
        return false;
    }
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicCoreFixedRegisterBinding *fixed_binding = NULL;
        MinicCoreTypeId core_pointee;

        if (binding->operand_index >= MINIC_RISCV64_ASM_MAX_OPERANDS ||
            bound[binding->operand_index] ||
            binding->value >= function->value_count ||
            (binding->early_clobber &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
            return false;
        }
        bound[binding->operand_index] = true;
        if (binding->has_fixed_register_binding) {
            fixed_binding =
                minic_riscv64_inline_asm_fixed_register_binding(function, binding->fixed_register_binding_id);
            if (fixed_binding == NULL || !fixed_binding->is_local) {
                return false;
            }
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!rv64_asm_core_pointer_scalar(function,
                    function->values[binding->value].core_type_id,
                    &core_pointee) ||
                (fixed_binding != NULL &&
                 !minic_lp64_core_equal_unqualified(function->type_context,
                     fixed_binding->core_type_id, core_pointee))) return false;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (fixed_binding != NULL ||
                !rv64_asm_core_pointer_scalar(function,
                    function->values[binding->value].core_type_id,
                    &core_pointee)) return false;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!rv64_asm_core_scalar(function,
                    function->values[binding->value].core_type_id) ||
                (fixed_binding != NULL &&
                 !minic_lp64_core_equal_unqualified(function->type_context,
                     fixed_binding->core_type_id,
                     function->values[binding->value].core_type_id))) return false;
            break;
        default:
            return false;
        }
    }
    for (template_index = 0U; template_index < inline_asm->template_length; ++template_index) {
        unsigned char ch;
        if (inline_asm->template_text[template_index] != '%') {
            continue;
        }
        if (++template_index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[template_index];
        if (ch == '%') {
            continue;
        }
        if (ch == 'z') {
            if (++template_index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[template_index];
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
    }
    return minic_riscv64_asm_instruction_plan_build(function, instruction, &probe);
}

/* M85_RECORD_CALL_ARGUMENT: validate direct-call arguments against the
   shared RV64 ABI classifier. Core stores VALUE/OBJECT form only; register
   placement remains entirely target-owned. The first caller tier intentionally
   stays register-only, matching the pre-M85 Core call backend's no-stack scope. */
static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const char *symbol_name,
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->is_goto) {
        bool deferred_immediate = inline_asm->source_inline_asm_id != SIZE_MAX;

        if (symbol_name == NULL || symbol_name[0] == '\0' ||
            inline_asm->goto_target >= function->block_count) {
            return false;
        }
        if (deferred_immediate) {
            if (fprintf(file,
                        "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                        "  .extern __minic_deferred_asm_immediate_%zu_0\n"
                        "  ",
                        inline_asm->source_inline_asm_id) < 0) {
                return false;
            }
        } else if (fprintf(file, "  ") < 0) {
            return false;
        }
        if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
            if (inline_asm->template_text[index] != '%') {
                if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                    return false;
                }
                continue;
            }
            if (index + 1U >= inline_asm->template_length) {
                return false;
            }
            if (inline_asm->template_text[index + 1U] == '%') {
                if (fputc('%', file) == EOF) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (inline_asm->template_text[index + 1U] == '0') {
                if (!deferred_immediate ||
                    fprintf(file,
                            "__minic_deferred_asm_immediate_%zu_0",
                            inline_asm->source_inline_asm_id) < 0) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (index + 2U < inline_asm->template_length &&
                inline_asm->template_text[index + 1U] == 'l' &&
                inline_asm->template_text[index + 2U] == '[') {
                size_t name_end = index + 3U;
                while (name_end < inline_asm->template_length &&
                       inline_asm->template_text[name_end] != ']') {
                    name_end += 1U;
                }
                if (name_end >= inline_asm->template_length || name_end == index + 3U ||
                    fprintf(file,
                            ".L%s_core_bb%" PRIu32,
                            symbol_name,
                            inline_asm->goto_target) < 0) {
                    return false;
                }
                index = name_end;
                continue;
            }
            return false;
        }
        return fputc('\n', file) != EOF &&
           minic_riscv64_asm_audit_marker(file, false);
    }
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            inline_asm->template_text[index + 1U] != '%') {
            return false;
        }
        if (fputc('%', file) == EOF) {
            return false;
        }
        index += 1U;
    }
    return fputc('\n', file) != EOF &&
           minic_riscv64_asm_audit_marker(file, false);
}

static bool emit_register_output_inline_asm(
    FILE *file,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_register_output_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        !minic_riscv64_asm_audit_marker(file, false) ||
        !rv64_asm_normalize_integer(file, function,
                                    instruction->core_type_id, "t0")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

static bool emit_register_output_input_inline_asm(
    FILE *file,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_register_output_input_inline_asm_supported(function, instruction) ||
        !minic_riscv64_frame_load_value(
            file, frame, instruction->value.register_output_input_inline_asm.operand, "t1")) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.register_output_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '1') {
            if (fprintf(file, "t1") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        !minic_riscv64_asm_audit_marker(file, false) ||
        !rv64_asm_normalize_integer(file, function,
                                    instruction->core_type_id, "t0")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

static bool emit_memory_readwrite_scalar_input_inline_asm(
    FILE *file,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t memory_index;
    size_t register_index;
    size_t scalar_index;
    size_t index;
    bool has_register_output;

    if (file == NULL || function == NULL || function->type_context == NULL || frame == NULL ||
        !core_memory_readwrite_scalar_input_inline_asm_supported(function, instruction) ||
        !minic_riscv64_frame_load_value(
            file,
            frame,
            instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
            "t0") ||
        !minic_riscv64_frame_load_value(
            file, frame, instruction->value.memory_readwrite_scalar_input_inline_asm.operand, "t2")) {
        return false;
    }
    memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
    register_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
    scalar_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
    has_register_output = register_index != SIZE_MAX;
    inline_asm = &function->inline_asms[
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
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
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_index == memory_index) {
            if (fprintf(file, "(t0)") < 0) {
                return false;
            }
        } else if (has_register_output && operand_index == register_index) {
            if (fprintf(file, "t1") < 0) {
                return false;
            }
        } else if (operand_index == scalar_index) {
            if (fprintf(file, "t2") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        !minic_riscv64_asm_audit_marker(file, false)) {
        return false;
    }
    if (!has_register_output) {
        return true;
    }
    if (!rv64_asm_normalize_integer(file, function,
                                    instruction->core_type_id, "t1")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
}

static bool emit_scalar_input_inline_asm(
    FILE *file,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_scalar_input_inline_asm_supported(function, instruction) ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.scalar_input_inline_asm.operand, "t0")) {
        return false;
    }
    inline_asm =
        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    return fputc('\n', file) != EOF &&
           minic_riscv64_asm_audit_marker(file, false);
}

static bool emit_structured_inline_asm(FILE *file,
                                       const MinicCoreFunction *function,
                                       const MinicRiscv64CoreFrame *frame,
                                       const MinicCoreInstruction *instruction,
                                       size_t instruction_id) {
    const MinicCoreInlineAsm *inline_asm;
    const MinicRiscv64AsmInstructionPlan *plan;
    const char *const *operand_registers;
    const bool *memory_operand;
    const char *scratch_register;
    const char *transfer_scratch_register;
    size_t binding_index;
    size_t index;

    if (file == NULL || frame == NULL || function == NULL ||
        function->type_context == NULL ||
        instruction == NULL ||
        (plan = minic_riscv64_asm_function_plan_get(&frame->asm_plan, instruction_id)) == NULL)
        return false;
    operand_registers = plan->operand_registers;
    memory_operand = plan->memory_operand;
    scratch_register = plan->scratch_register;
    transfer_scratch_register = plan->transfer_scratch_register;
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name = operand_registers[binding->operand_index];
        MinicCoreTypeId core_value_type = MINIC_CORE_TYPE_INVALID;

        if (register_name == NULL) {
            return false;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_riscv64_frame_load_value(file, frame, binding->value, scratch_register) ||
                !rv64_asm_core_pointer_scalar(function,
                    function->values[binding->value].core_type_id,
                    &core_value_type) ||
                !rv64_asm_emit_scalar_load(
                    file, function, core_value_type,
                    register_name, scratch_register,
                    transfer_scratch_register,
                    minic_riscv_address_alignment(
                        function, binding->value))) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!minic_riscv64_frame_load_value(file, frame, binding->value, register_name)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    if (fprintf(file, "  ") < 0) {
        return false;
    }
    if (!minic_riscv64_asm_audit_marker(file, true)) return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;
        size_t operand_index;
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (++index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch == 'z') {
            if (++index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[index];
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_registers[operand_index] == NULL) {
            return false;
        }
        if (memory_operand[operand_index]) {
            if (fprintf(file, "(%s)", operand_registers[operand_index]) < 0) {
                return false;
            }
        } else if (fprintf(file, "%s", operand_registers[operand_index]) < 0) {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        !minic_riscv64_asm_audit_marker(file, false)) {
        return false;
    }

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        MinicCoreTypeId core_value_type = MINIC_CORE_TYPE_INVALID;

        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            continue;
        }
        register_name = operand_registers[binding->operand_index];
        if (!rv64_asm_core_pointer_scalar(function,
                function->values[binding->value].core_type_id,
                &core_value_type) ||
            !rv64_asm_normalize_integer(file, function,
                                        core_value_type, register_name) ||
            !minic_riscv64_frame_load_value(file, frame, binding->value, scratch_register) ||
            !rv64_asm_emit_scalar_store(
                file, function, core_value_type,
                register_name, scratch_register,
                transfer_scratch_register,
                minic_riscv_address_alignment(
                    function, binding->value))) {
            return false;
        }
    }
    return true;
}


bool minic_riscv64_inline_asm_instruction_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    if (function == NULL || instruction == NULL ||
        function->inline_asms == NULL ||
        instruction->value.inline_asm_id >= function->inline_asm_count ||
        !minic_riscv64_source_asm_marker_safe(
            function->inline_asms[instruction->value.inline_asm_id].template_text,
            function->inline_asms[instruction->value.inline_asm_id].template_length))
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return core_opaque_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return core_register_output_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return core_register_output_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return core_memory_readwrite_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return core_structured_inline_asm_supported(function, instruction);
    default: return false;
    }
}
bool minic_riscv64_emit_inline_asm_instruction(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const char *symbol_name, const MinicCoreInstruction *instruction,
    size_t instruction_id) {
    if (file == NULL || function == NULL || frame == NULL || instruction == NULL ||
        !minic_riscv64_inline_asm_instruction_supported(function, instruction))
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, symbol_name, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return emit_register_output_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return emit_register_output_input_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return emit_memory_readwrite_scalar_input_inline_asm(
            file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return emit_scalar_input_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return emit_structured_inline_asm(file, function, frame, instruction, instruction_id);
    default: return false;
    }
}
