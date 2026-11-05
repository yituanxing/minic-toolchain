#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv/typed_memory.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/isa.h"
#include "target/common/lp64_scalar.h"
#include "core/symbol_identity.h"
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

/* Opaque ASM is a distinct, operand-free Core effect. GNU immediate lowering
 * has already substituted its symbolic operands into the template; unlike
 * the retired scalar ASM opcodes, no target register binding remains. */
static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,
                                             const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !rv64_asm_void(function, instruction->core_type_id) ||
        instruction->value.inline_asm_id >= function->inline_asm_count) return false;
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    return inline_asm->template_text != NULL && inline_asm->is_volatile &&
        (!inline_asm->is_goto ||
         (inline_asm->goto_target < function->block_count &&
          inline_asm->goto_second_target == MINIC_CORE_BLOCK_INVALID));
}

static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const char *symbol_name,
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;
    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction))
        return false;
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->is_goto &&
        (symbol_name == NULL || symbol_name[0] == '\0')) return false;
    if (inline_asm->is_goto && inline_asm->deferred_immediate_symbol != NULL &&
        (!minic_core_deferred_asm_symbol_valid(inline_asm->deferred_immediate_symbol) ||
         fprintf(file, "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                       "  .extern %s\n",
                 inline_asm->deferred_immediate_symbol) < 0)) return false;
    if (fprintf(file, "  ") < 0 || !minic_riscv64_asm_audit_marker(file, true))
        return false;
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF)
                return false;
            continue;
        }
        if (++index >= inline_asm->template_length) return false;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) return false;
            continue;
        }
        if (inline_asm->is_goto && ch == '0' &&
            inline_asm->deferred_immediate_symbol != NULL) {
            if (fputs(inline_asm->deferred_immediate_symbol, file) == EOF) return false;
            continue;
        }
        if (inline_asm->is_goto && ch == 'l' &&
            index + 1U < inline_asm->template_length &&
            inline_asm->template_text[index + 1U] == '[') {
            index += 2U;
            if (index >= inline_asm->template_length ||
                inline_asm->template_text[index] == ']') return false;
            while (index < inline_asm->template_length &&
                   inline_asm->template_text[index] != ']') ++index;
            if (index >= inline_asm->template_length ||
                fprintf(file, ".L%s_core_bb%" PRIu32,
                        symbol_name, inline_asm->goto_target) < 0) return false;
            continue;
        }
        return false;
    }
    return fputc('\n', file) != EOF &&
        minic_riscv64_asm_audit_marker(file, false);
}

/* M126A_GENERIC_STRUCTURED_ASM: capability is now role/resource based. */
static bool core_structured_inline_asm_supported(const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicRiscv64AsmInstructionPlan probe;
    bool bound[MINIC_RISCV64_ASM_MAX_OPERANDS] = {false};
    size_t binding_index;
    size_t template_index;
    size_t scalar_results = 0U;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        (instruction->result == MINIC_CORE_VALUE_INVALID
             ? !rv64_asm_void(function, instruction->core_type_id)
             : (instruction->result >= function->value_count ||
                !rv64_asm_integer_or_pointer(function, instruction->core_type_id))) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile ||
        (inline_asm->is_goto &&
         (instruction->value.structured_inline_asm.operand_count != 0U ||
          inline_asm->goto_target >= function->block_count ||
          inline_asm->goto_second_target != MINIC_CORE_BLOCK_INVALID))) {
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
            (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT &&
             binding->value >= function->value_count) ||
            (binding->early_clobber &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT)) {
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
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT:
            if (++scalar_results != 1U ||
                instruction->result == MINIC_CORE_VALUE_INVALID ||
                binding->value != MINIC_CORE_VALUE_INVALID ||
                fixed_binding != NULL || binding->has_fixed_register_binding)
                return false;
            break;
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
    if ((instruction->result == MINIC_CORE_VALUE_INVALID) !=
        (scalar_results == 0U)) return false;
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
        if (inline_asm->is_goto) {
            if (ch == '0' && inline_asm->deferred_immediate_symbol != NULL)
                continue;
            if (ch == 'l' && template_index + 1U < inline_asm->template_length &&
                inline_asm->template_text[template_index + 1U] == '[') {
                template_index += 2U;
                if (template_index >= inline_asm->template_length ||
                    inline_asm->template_text[template_index] == ']') return false;
                while (template_index < inline_asm->template_length &&
                       inline_asm->template_text[template_index] != ']')
                    ++template_index;
                if (template_index >= inline_asm->template_length) return false;
                continue;
            }
            return false;
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

/* One machine emission owner for all canonical structured ASM roles, including
 * unbound raw ASM/asm-goto effects and typed register-result SSA. */
static bool emit_structured_inline_asm(FILE *file,
                                       const MinicCoreFunction *function,
                                       const MinicRiscv64CoreFrame *frame,
                                       const char *symbol_name,
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
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT:
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

    if (inline_asm->is_goto && inline_asm->deferred_immediate_symbol != NULL &&
        (!minic_core_deferred_asm_symbol_valid(inline_asm->deferred_immediate_symbol) ||
         fprintf(file,
                 "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                 "  .extern %s\n",
                 inline_asm->deferred_immediate_symbol) < 0)) return false;
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
        if (inline_asm->is_goto) {
            if (ch == '0') {
                if (inline_asm->deferred_immediate_symbol == NULL ||
                    fputs(inline_asm->deferred_immediate_symbol, file) == EOF)
                    return false;
                continue;
            }
            if (ch == 'l' && index + 1U < inline_asm->template_length &&
                inline_asm->template_text[index + 1U] == '[') {
                index += 2U;
                if (index >= inline_asm->template_length ||
                    inline_asm->template_text[index] == ']') return false;
                while (index < inline_asm->template_length &&
                       inline_asm->template_text[index] != ']') ++index;
                if (index >= inline_asm->template_length || symbol_name == NULL ||
                    fprintf(file, ".L%s_core_bb%" PRIu32,
                            symbol_name, inline_asm->goto_target) < 0)
                    return false;
                continue;
            }
            return false;
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

        if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT) {
            register_name = operand_registers[binding->operand_index];
            if (instruction->result == MINIC_CORE_VALUE_INVALID ||
                !rv64_asm_normalize_integer(file, function,
                                            instruction->core_type_id, register_name) ||
                !minic_riscv64_frame_store_value(
                    file, frame, instruction->result, register_name)) return false;
            continue;
        }
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
    const MinicCoreInlineAsm *inline_asm;
    if (function == NULL || instruction == NULL || function->inline_asms == NULL)
        return false;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
        if (!core_opaque_inline_asm_supported(function, instruction)) return false;
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    } else if (instruction->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
        if (!core_structured_inline_asm_supported(function, instruction)) return false;
        inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    } else return false;
    return minic_riscv64_source_asm_marker_safe(inline_asm->template_text,
                                                inline_asm->template_length);
}
bool minic_riscv64_emit_inline_asm_instruction(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const char *symbol_name, const MinicCoreInstruction *instruction,
    size_t instruction_id) {
    if (file == NULL || function == NULL || frame == NULL || instruction == NULL ||
        !minic_riscv64_inline_asm_instruction_supported(function, instruction))
        return false;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
        return emit_opaque_inline_asm(file, function, symbol_name, instruction);
    return emit_structured_inline_asm(file, function, frame, symbol_name,
                                      instruction, instruction_id);
}
