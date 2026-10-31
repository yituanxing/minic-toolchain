#include "target/riscv64/core_codegen.h"
#include "target/riscv64/core_admission.h"
#include "target/riscv64/function_abi_plan.h"

#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/call_emit.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/inline_asm_emit.h"
#include "target/riscv64/memory_emit.h"
#include "target/riscv64/frame_layout.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/scalar_emit.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/legalize.h"
#include "target/riscv64/runtime_libcall.h"
#include "target/riscv/instruction_select.h"
#include "core/value_uses.h"
#include "target/common/core_block_walk.h"
#include "target/common/core_instruction_family.h"
#include "target/data_layout.h"
#include "target/target_info.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool core_frame_fail(MinicRiscv64CoreFrame *frame) {
    minic_riscv64_core_frame_destroy(frame);
    return false;
}
static bool emit_instruction(FILE *file,
                             const MinicC0Program *program,
                             const MinicCoreFunction *function,
                             const MinicRiscv64CoreFrame *frame,
                             const char *symbol_name,
                             const MinicCoreInstruction *instruction,
                             size_t instruction_id,
                             bool position_independent,
                             const MinicRiscvIsa *isa) {
    size_t object_offset;

    /* All Core instruction IDs were admitted once before function entry;
     * no duplicate shape checks during per-opcode machine emission. */
    if (file == NULL || function == NULL || frame == NULL ||
        symbol_name == NULL || instruction == NULL) {
        return false;
    }
    /* Core family ownership is common across five targets. The RV64
     * machine scalar emitter still handles actual ISA/ABI legalization. */
    if (minic_core_instruction_is_integer_scalar(instruction->kind) ||
        minic_core_instruction_is_floating_scalar(instruction->kind) ||
        instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        instruction->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST)
        return minic_riscv64_emit_scalar_instruction(
            file, program, function, frame, symbol_name, instruction, isa);
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        /* SysV 24-byte cursor copying is x86-only; RV64 keeps its linear
         * cursor-assignment path and must reject this opcode explicitly. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        return false;
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        const size_t size = instruction->value.pointer_offset.element_size;
        MinicRiscvPointerScalePlan scale;
        if (!minic_riscv_plan_pointer_scale(size, &scale) ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.pointer_offset.base, "t0"))
            return false;
        /* GNU empty-record pointer arithmetic keeps its base. The index
         * expression was already evaluated when constructing Core SSA. */
        if (scale.kind == MINIC_RISCV_POINTER_SCALE_ZERO)
            return minic_riscv64_frame_store_value(
                file, frame, instruction->result, "t0");
        if (!minic_riscv64_frame_load_value(
                file, frame, instruction->value.pointer_offset.index, "t1"))
            return false;
        if (scale.kind != MINIC_RISCV_POINTER_SCALE_UNIT) {
            if (isa != NULL && isa->strict_isa &&
                !minic_riscv_codegen_has(isa, MINIC_RISCV_FEATURE_M)) {
                if (scale.kind == MINIC_RISCV_POINTER_SCALE_SHIFT) {
                    if (fprintf(file, "  slli t1, t1, %u\n", scale.shift) < 0)
                        return false;
                } else if (fprintf(file, "  li t2, %zu\n", size) < 0 ||
                           !minic_riscv64_emit_arithmetic_libcall(
                               file, isa, MINIC_RISCV64_LIBCALL_MUL, "t1", "t1", "t2")) {
                    return false;
                }
            } else if (fprintf(file,
                               "  li t2, %zu\n"
                               "  mul t1, t1, t2\n", size) < 0) {
                return false;
            }
        }
        /* A runtime call clobbers t0. Reload the Core-owned base only for
         * odd-stride Base-I multiplication, not zero/unit/shift paths. */
        if (minic_riscv64_uses_arithmetic_libcall(isa) &&
            scale.kind == MINIC_RISCV_POINTER_SCALE_MULTIPLY &&
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.pointer_offset.base, "t0"))
            return false;
        /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: retain subtraction through
           Core so an unsigned index is scaled before `sub`. */
        if (fprintf(file, "  %s t0, t0, t1\n",
                    instruction->value.pointer_offset.subtract ? "sub" : "add") < 0)
            return false;
        return minic_riscv64_frame_store_value(
            file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = minic_riscv64_inline_asm_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U || !minic_riscv64_scalar_type(binding->type) ||
            !minic_type_equal(binding->type, instruction->type) ||
            fprintf(file, "  mv t0, %s\n", binding->register_name) < 0) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        if (!minic_riscv64_core_call_frame_address_supported(instruction)) {
            return false;
        }
        if (instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN) {
            if (!frame->saves_return_address ||
                !minic_riscv64_frame_emit_load64(file, frame, "t0", frame->return_address_offset)) {
                return false;
            }
        } else if (instruction->value.call_frame_address.kind ==
                   MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) {
            if (fprintf(file,
                        frame->has_dynamic_stack_alloc ? "  mv t0, s0\n" : "  mv t0, sp\n") < 0) {
                return false;
            }
        } else {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        if (!frame->has_dynamic_stack_alloc ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  addi t0, t0, 15\n"
                    "  li t1, -16\n"
                    "  and t0, t0, t1\n"
                    "  sub sp, sp, t0\n") < 0) {
            return false;
        }
        if (frame->dynamic_call_area_size == 0U) {
            if (fprintf(file, "  mv t0, sp\n") < 0) {
                return false;
            }
        } else if (frame->dynamic_call_area_size <= 2047U) {
            if (fprintf(file,
                        "  addi t0, sp, %zu\n",
                        frame->dynamic_call_area_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t0, %zu\n"
                           "  add t0, sp, t0\n",
                           frame->dynamic_call_area_size) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        if (!frame->has_variadic_argument_address) {
            return false;
        }
        if (frame->varargs_size != 0U) {
            if (!minic_riscv64_frame_emit_address(file, frame, "t0", frame->varargs_offset)) {
                return false;
            }
        } else {
            size_t stack_byte_offset;

            if (frame->variadic_fixed_stack_slots > SIZE_MAX / 8U) {
                return false;
            }
            stack_byte_offset = frame->variadic_fixed_stack_slots * 8U;
            if (frame->has_dynamic_stack_alignment) {
                if (!minic_riscv64_frame_emit_load64(file, frame, "t0", frame->entry_sp_offset)) {
                    return false;
                }
            } else {
                if (frame->frame_size <= 2047U) {
                    if (fprintf(file,
                                frame->has_dynamic_stack_alloc
                                    ? "  addi t0, s0, %zu\n"
                                    : "  addi t0, sp, %zu\n",
                                frame->frame_size) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   frame->has_dynamic_stack_alloc
                                       ? "  li t0, %zu\n  add t0, s0, t0\n"
                                       : "  li t0, %zu\n  add t0, sp, t0\n",
                                   frame->frame_size) < 0) {
                    return false;
                }
            }
            if (stack_byte_offset != 0U) {
                if (stack_byte_offset <= 2047U) {
                    if (fprintf(file, "  addi t0, t0, %zu\n", stack_byte_offset) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   "  li t1, %zu\n"
                                   "  add t0, t0, t1\n",
                                   stack_byte_offset) < 0) {
                    return false;
                }
            }
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return minic_riscv64_emit_incoming_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return minic_riscv64_emit_incoming_parameter_object(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!minic_riscv64_core_frame_object_offset(program, function, frame, instruction->value.object_id, &object_offset) ||
            !minic_riscv64_frame_emit_address(file, frame, "t0", object_offset)) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        if (instruction->value.block_id >= function->block_count ||
            fprintf(file, "  la t0, .L%s_core_bb%" PRIu32 "\n", symbol_name, instruction->value.block_id) < 0) return false;
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    /* Static Linux kernels need lla, not GOT, before the MMU is on.
     * GNU/glibc PIE must instead resolve preemptible objects/functions via
     * the GOT. Keep the C ABI decision here in RV64 Target emission. Scope
     * .option pic with push/pop so user file/inline asm cannot leak modes. */
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count)
            return false;
        /* R_RISCV_GOT_HI20 is needed only for a preemptible symbol. In
         * particular, a relocatable Linux kernel may compile with -fPIE,
         * but its private literals and hidden objects must remain PC-relative:
         * the early bootstrap cannot dereference a GOT slot containing the
         * linked high virtual address before enabling the MMU. */
        if (fprintf(file,
                    position_independent &&
                            !minic_target_info_elf_object_is_nonpreemptible(
                                program, function->globals[instruction->value.global_id].name)
                        ? "  .option push\n  .option pic\n  la t0, %s\n  .option pop\n"
                        : "  lla t0, %s\n",
                    function->globals[instruction->value.global_id].name) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (instruction->value.function_symbol_id >= function->function_symbol_count)
            return false;
        if (fprintf(file,
                    position_independent &&
                            !minic_target_info_elf_function_is_nonpreemptible(
                                program,
                                function->function_symbols[
                                    instruction->value.function_symbol_id].name)
                        ? "  .option push\n  .option pic\n  la t0, %s\n  .option pop\n"
                        : "  lla t0, %s\n",
                    function->function_symbols[instruction->value.function_symbol_id].name) < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return minic_riscv64_emit_memory_instruction(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return minic_riscv64_emit_inline_asm_instruction(
            file, program, function, frame, symbol_name, instruction, instruction_id);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_emit_direct_call(file, program, function, frame, instruction, instruction_id);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_emit_indirect_call(file, program, function, frame, instruction, instruction_id);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return minic_riscv64_emit_memory_instruction(file, program, function, frame, instruction);
    default:
        return false;
    }
    return false;
}

static bool emit_block_label(FILE *file, const char *symbol_name, MinicCoreBlockId block_id) {
    return fprintf(file, ".L%s_core_bb%" PRIu32 ":\n", symbol_name, block_id) >= 0;
}

/* Bootstrap B0 reaches functions large enough that a direct JAL relocation can
   overflow its signed 21-bit displacement. Core values are stack-backed at
   terminators, so t6 is a dead scratch register here. Materialize local CFG
   destinations PC-relatively and jump through t6; this keeps block transfers
   valid across the whole practical function-size range instead of relying on
   assembler/linker JAL relaxation. */
static bool emit_far_jump_to_block(FILE *file,
                                   const char *symbol_name,
                                   MinicCoreBlockId block_id) {
    return file != NULL && symbol_name != NULL &&
           fprintf(file,
                   "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                   "  jalr zero, t6, 0\n",
                   symbol_name,
                   block_id) >= 0;
}

static bool emit_far_jump_to_return(FILE *file, const char *symbol_name) {
    return file != NULL && symbol_name != NULL &&
           fprintf(file,
                   "  lla t6, .L%s_core_return\n"
                   "  jalr zero, t6, 0\n",
                   symbol_name) >= 0;
}

static bool emit_terminator(FILE *file,
                            const MinicC0Program *program,
                            const MinicCoreFunction *function,
                            const MinicRiscv64CoreFrame *frame,
                            const char *symbol_name,
                            const MinicCoreTerminator *terminator) {
    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL ||
        terminator == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_record(function->return_type)) {
            /* FIXME(MC-057): classify_value can return ABI_VALUE_IGNORE for
             * a GNU zero-byte Record; this return emitter handles only
             * AGGREGATE/INDIRECT and must model no physical return payload. */
            MinicRiscv64AbiValue return_value;
            size_t object_offset;

            if (program == NULL || terminator->return_object >= function->object_count ||
                !minic_type_equal(function->objects[terminator->return_object].type,
                                  function->return_type) ||
                !minic_riscv64_abi_classify_value_profile(
                    program, function->return_type,
                    frame->named_parameter_cursor.lp64_soft_float,
                    &return_value) ||
                !minic_riscv64_core_frame_object_offset(program, function, frame, terminator->return_object, &object_offset) ||
                !minic_riscv64_frame_emit_address(file, frame, "t0", object_offset)) {
                return false;
            }
            if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (return_value.slot_count == 0U || return_value.slot_count > 2U ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk_profile(
                        file, program, function->return_type, 0U, "a0", "t0",
                        frame->named_parameter_cursor.lp64_soft_float, object_offset) ||
                    (return_value.slot_count == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk_profile(
                         file, program, function->return_type, 1U, "a1", "t0",
                         frame->named_parameter_cursor.lp64_soft_float, object_offset))) {
                    return false;
                }
            } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                size_t copied;

                if (!frame->has_hidden_result_pointer || return_value.storage_size <= 16U ||
                    return_value.slot_count != 1U ||
                    !minic_riscv64_frame_emit_load64(
                        file, frame, "t1", frame->hidden_result_pointer_offset)) {
                    return false;
                }
                copied = 0U;
                while (copied < return_value.storage_size) {
                    size_t chunk = return_value.storage_size - copied;
                    size_t offset;

                    if (chunk > 2048U) {
                        chunk = 2048U;
                    }
                    for (offset = 0U; offset < chunk; ++offset) {
                        if (fprintf(file,
                                    "  lbu t2, %zu(t0)\n"
                                    "  sb t2, %zu(t1)\n",
                                    offset,
                                    offset) < 0) {
                            return false;
                        }
                    }
                    copied += chunk;
                    if (copied < return_value.storage_size &&
                        fprintf(file,
                                "  addi t0, t0, 2047\n"
                                "  addi t0, t0, 1\n"
                                "  addi t1, t1, 2047\n"
                                "  addi t1, t1, 1\n") < 0) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        } else if (minic_type_is_long_double(function->return_type) ||
                   minic_type_is_int128_integer(function->return_type)) {
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !minic_riscv64_frame_load_int128(
                    file, frame, terminator->return_value, "a0", "a1")) {
                return false;
            }
        } else if (!frame->named_parameter_cursor.lp64_soft_float &&
                   (minic_type_is_float(function->return_type) ||
                    minic_type_is_double(function->return_type))) {
            const char *move_opcode =
                minic_type_is_double(function->return_type) ? "fmv.d.x" : "fmv.w.x";
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !minic_riscv64_frame_load_value(file, frame, terminator->return_value, "a0") ||
                fprintf(file, "  %s fa0, a0\n", move_opcode) < 0) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !minic_riscv64_frame_load_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return emit_far_jump_to_return(file, symbol_name);
    /* M91_BUILTIN_UNREACHABLE_TERMINATOR: reaching this block is UB; no
       target instruction is required. The Core terminator still prevents
       normal CFG fallthrough from being modeled as a supported continuation. */
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return emit_far_jump_to_block(file, symbol_name, terminator->branch_target);
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_RV64 */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return minic_riscv64_frame_load_value(file, frame, terminator->indirect_target, "t0") &&
               fprintf(file, "  jalr zero, t0, 0\n") >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        /* Keep the architectural conditional branch local, then use the same
           far-transfer sequence for both CFG successors. The numeric local
           label is intentionally reusable; 1f always resolves to the next
           instance emitted by this terminator. */
        return minic_riscv64_frame_load_value(file, frame, terminator->conditional.condition, "t0") &&
               fprintf(file,
                       "  beqz t0, 1f\n"
                       "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                       "  jalr zero, t6, 0\n"
                       "1:\n"
                       "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                       "  jalr zero, t6, 0\n",
                       symbol_name,
                       terminator->conditional.when_true,
                       symbol_name,
                       terminator->conditional.when_false) >= 0;
    }
    return false;
}

/* M177_CONSTANT_SWITCH_CFG_OWNER: user C labels are valid re-entry
   roots for goto/asm-goto/computed-goto, so seed them conservatively alongside
   the function entry and close over explicit Core terminator edges. Synthetic
   switch blocks with no executable predecessor remain unmarked. */
static bool emit_core_function_with_symbol(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicCoreFunction *function,
                                                    const MinicRiscvFunctionSymbol *symbol,
                                                    bool position_independent,
                                                    const MinicRiscvIsa *isa,
                                                    MinicDiagnostic *diagnostic,
                                                    const MinicCoreFunctionFacts *facts,
                                                    const MinicCoreObjectStoragePlan *storage) {
    MinicRiscv64CoreFrame frame;
    MinicRiscv64FunctionAbiPlan abi_plan = {0};
    MinicCoreFunctionFacts compatibility_facts;
    const char *symbol_name;
    size_t block_index;
    MinicCoreBlockWalk machine_blocks = {0};
    bool bootstrap_trace;

    if (file == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        symbol->symbol_name[0] == '\0') {
        return false;
    }
    /* Legacy standalone Core emitters have no common TU visitor. Preserve
     * their ABI/frame contract without asking production to rescan Core. */
    if (facts == NULL) {
        if (!minic_core_function_facts_analyze(function, &compatibility_facts))
            return false;
        facts = &compatibility_facts;
    }
    symbol_name = symbol->symbol_name;
    bootstrap_trace = getenv("MINIC_BOOTSTRAP_TRACE") != NULL;
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-capability state=begin "
                      "function=%s blocks=%zu instructions=%zu values=%zu\n",
                      symbol_name,
                      function != NULL ? function->block_count : 0U,
                      function != NULL ? function->instruction_count : 0U,
                      function != NULL ? function->value_count : 0U);
        (void)fflush(stderr);
    }
    if (!minic_riscv64_core_function_admit_profile(
            program, function, &abi_plan,
            isa != NULL && isa->strict_isa &&
            isa->abi == MINIC_RISCV_ABI_LP64, facts, storage)) {
        minic_riscv64_function_abi_plan_destroy(&abi_plan);
        return false;
    }
    memset(&frame, 0, sizeof(frame));
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-capability state=end "
                      "function=%s\n",
                      symbol_name);
        (void)fflush(stderr);
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=begin "
                      "function=%s\n",
                      symbol_name);
        (void)fflush(stderr);
    }
    /* Cross-block sharing policy is a function/frame input, not global. */
    {
        const char *focus = getenv("MINIC_P1_CROSSBLOCK_FUNCTION");
        const bool crossblock_enabled =
            focus == NULL || focus[0] == '\0' ||
            strcmp(focus, symbol_name) == 0;
        if (!minic_riscv64_asm_function_plan_build(function, &frame.asm_plan) ||
            !minic_riscv64_core_frame_initialize_with_storage(
                program, function, &frame,
                program != NULL ? &abi_plan : NULL, crossblock_enabled,
                minic_riscv64_needs_arithmetic_libcall(isa, function), facts, storage)) {
            minic_riscv64_function_abi_plan_destroy(&abi_plan);
            return core_frame_fail(&frame);
        }
    }
    minic_riscv64_function_abi_plan_destroy(&abi_plan);
    if (!minic_riscv64_core_frame_mark_reachable_blocks(function, &frame)) {
        return core_frame_fail(&frame);
    }
    /* ISA-specific instruction selection occurs AFTER actual CFG reachability
     * is known but BEFORE any function machine code is written. No Core
     * semantic restriction and no second frame/ABI analysis are introduced.
     * Only explicit ISA profiles pay for this pass. The output auditor
     * remains a final guard for implicit frame/call/helper instructions. */
    if (isa != NULL && isa->strict_isa) {
        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            const MinicCoreBlock *block = &function->blocks[block_index];
            size_t j;
            if (frame.reachable_blocks != NULL && !frame.reachable_blocks[block_index])
                continue;
            for (j = 0U; j < block->instruction_count; ++j) {
                MinicCoreInstructionId id = block->instructions[j];
                char reason[220];
                if (id >= function->instruction_count ||
                    !minic_riscv_isa_admit_core_instruction(
                        isa, function, id < function->instruction_count ?
                        &function->instructions[id] : NULL,
                        reason, sizeof(reason))) {
                    char message[256];
                    (void)snprintf(message, sizeof(message),
                                   "function %.64s block=%zu instruction=%u: %.64s",
                                   symbol_name, block_index, (unsigned int)id,
                                   id < function->instruction_count ?
                                       reason : "invalid Core instruction id");
                    minic_riscv64_set_diagnostic(diagnostic, "<riscv-isa>", message);
                    return core_frame_fail(&frame);
                }
            }
        }
    }
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=end "
                      "function=%s frame_size=%zu value_slots=%zu values=%zu\n",
                      symbol_name,
                      frame.frame_size,
                      frame.value_slot_count,
                      frame.value_count);
        (void)fflush(stderr);
    }
    if (!minic_riscv_emit_function_symbol_begin(file, symbol)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_dynamic_stack_alignment) {
        if (fprintf(file, "  mv t0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.frame_size) ||
            fprintf(file,
                    "  li t1, -%zu\n"
                    "  and sp, sp, t1\n",
                    frame.stack_alignment) < 0 ||
            !minic_riscv64_emit_sp_store64(file, "t0", frame.entry_sp_offset)) {
            return core_frame_fail(&frame);
        }
    } else if (!minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_dynamic_stack_alloc) {
        if (!minic_riscv64_emit_sp_store64(
                file, "s0", frame.saved_frame_base_offset) ||
            fprintf(file, "  mv s0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.dynamic_call_area_size)) {
            return core_frame_fail(&frame);
        }
    }
    if (frame.saves_return_address &&
        !minic_riscv64_frame_emit_store64(file, &frame, "ra", frame.return_address_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_hidden_result_pointer &&
        !minic_riscv64_frame_emit_store64(file, &frame, "a0", frame.hidden_result_pointer_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.asm_plan.callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;
            if (!minic_riscv64_frame_emit_store64(
                    file, &frame, minic_riscv64_asm_callee_saved_registers[saved_index], saved_offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (frame.has_variadic_argument_address) {
        size_t register_index;

        for (register_index = frame.integer_parameter_count; register_index < 8U;
             ++register_index) {
            size_t offset = frame.varargs_offset +
                            (register_index - frame.integer_parameter_count) * 8U;
            if (!minic_riscv64_frame_emit_store64(
                    file, &frame, minic_riscv64_abi_integer_argument_registers[register_index], offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (!emit_far_jump_to_block(file, symbol_name, function->entry_block)) {
        return core_frame_fail(&frame);
    }
    minic_core_block_walk_begin(&machine_blocks, function, frame.reachable_blocks);
    {
    const MinicCoreBlock *block;
    while (minic_core_block_walk_next(&machine_blocks, &block_index, &block)) {
        size_t instruction_index;

        if (bootstrap_trace &&
            (block_index == 0U || block_index % 128U == 0U ||
             block_index + 1U == function->block_count)) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-codegen-block "
                          "function=%s block=%zu total=%zu\n",
                          symbol_name,
                          block_index,
                          function->block_count);
            (void)fflush(stderr);
        }
        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {
            return core_frame_fail(&frame);
        }
        if (!function->suppress_source_label_aliases &&
            block->source_label_id != SIZE_MAX &&
            fprintf(file, ".Luser_%zu:\n", block->source_label_id) < 0) {
            return core_frame_fail(&frame);
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            MinicCoreInstructionId instruction_id = 0U;
            const MinicCoreInstruction *ins = NULL;
            if (!minic_core_block_instruction_at(function, block, instruction_index,
                                                 &instruction_id, &ins) ||
                !emit_instruction(file, program, function, &frame, symbol_name,
                                  ins, (size_t)instruction_id, position_independent, isa)) {
                return core_frame_fail(&frame);
            }
        }
        if (!block->has_terminator ||
            !emit_terminator(file, program, function, &frame, symbol_name, &block->terminator)) {
            return core_frame_fail(&frame);
        }
    }
    }
    if (fprintf(file, ".L%s_core_return:\n", symbol_name) < 0) {
        return core_frame_fail(&frame);
    }
    if (frame.saves_return_address &&
        !minic_riscv64_frame_emit_load64(file, &frame, "ra", frame.return_address_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.asm_plan.callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;
            if (!minic_riscv64_frame_emit_load64(
                    file, &frame, minic_riscv64_asm_callee_saved_registers[saved_index], saved_offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (frame.has_dynamic_stack_alloc) {
        if (!minic_riscv64_frame_emit_load64(file, &frame, "t4", frame.saved_frame_base_offset)) {
            return core_frame_fail(&frame);
        }
        if (frame.has_dynamic_stack_alignment) {
            if (!minic_riscv64_frame_emit_load64(file, &frame, "t0", frame.entry_sp_offset)) {
                return core_frame_fail(&frame);
            }
        } else if (frame.frame_size <= 2047U) {
            if (fprintf(file, "  addi t0, s0, %zu\n", frame.frame_size) < 0) {
                return core_frame_fail(&frame);
            }
        } else if (fprintf(file,
                           "  li t0, %zu\n"
                           "  add t0, s0, t0\n",
                           frame.frame_size) < 0) {
            return core_frame_fail(&frame);
        }
        if (fprintf(file,
                    "  mv sp, t0\n"
                    "  mv s0, t4\n") < 0) {
            return core_frame_fail(&frame);
        }
    } else if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\n") < 0) {
            return core_frame_fail(&frame);
        }
    } else if (!minic_riscv64_emit_stack_release(file, frame.frame_size)) {
        return core_frame_fail(&frame);
    }
    if (fprintf(file, "  ret\n") < 0 ||
        !minic_riscv_emit_function_symbol_end(file, symbol)) {
        return core_frame_fail(&frame);
    }
    minic_riscv64_core_frame_destroy(&frame);
    return true;
}

bool minic_riscv64_emit_core_function_with_symbol(
    FILE *file, const MinicCoreFunction *function, const MinicRiscvFunctionSymbol *symbol) {
    return emit_core_function_with_symbol(file, NULL, function, symbol, false,
                                          NULL, NULL, NULL, NULL);
}

bool minic_riscv64_emit_core_function_for_program_with_symbol_with_storage(
    FILE *file, const MinicC0Program *program, const MinicCoreFunction *function,
    const MinicRiscvFunctionSymbol *symbol, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, bool position_independent,
    const MinicRiscvIsa *isa, MinicDiagnostic *diagnostic) {
    return program != NULL && facts != NULL &&
           emit_core_function_with_symbol(file, program, function, symbol,
                                          position_independent, isa, diagnostic,
                                          facts, storage);
}

bool minic_riscv64_emit_core_function_for_program_with_symbol(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscvFunctionSymbol *symbol,
    bool position_independent,
    const MinicRiscvIsa *isa,
    MinicDiagnostic *diagnostic) {
    MinicCoreFunctionFacts facts;
    return program != NULL &&
           minic_core_function_facts_analyze(function, &facts) &&
           emit_core_function_with_symbol(file, program, function, symbol,
                                          position_independent, isa, diagnostic,
                                          &facts, NULL);
}
