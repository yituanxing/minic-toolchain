#include "target/riscv64/call_emit.h"
#include "target/common/core_call_view.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include <stdint.h>
#include <stdio.h>

/* Keep the proven direct/indirect psABI marshalling and the inbound paths
 * together; the frame and ABI cursor both remain independent owners. */
static bool emit_incoming_stack_load64(FILE *file,
                                               const MinicRiscv64CoreFrame *frame,
                                               const char *destination_register,
                                               size_t stack_slot) {
    size_t byte_offset;

    if (file == NULL || frame == NULL || destination_register == NULL ||
        stack_slot > SIZE_MAX / 8U) {
        return false;
    }
    byte_offset = stack_slot * 8U;
    if (!frame->has_dynamic_stack_alignment) {
        if (byte_offset > SIZE_MAX - frame->frame_size) {
            return false;
        }
        if (!frame->has_dynamic_stack_alloc) {
            return minic_riscv64_emit_sp_load64(
                file, destination_register, frame->frame_size + byte_offset);
        }
        if (frame->frame_size <= 2047U) {
            if (fprintf(file, "  addi t3, s0, %zu\n", frame->frame_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t2, %zu\n"
                           "  add t3, s0, t2\n",
                           frame->frame_size) < 0) {
            return false;
        }
    } else if (!minic_riscv64_frame_emit_load64(file, frame, "t3", frame->entry_sp_offset)) {
        return false;
    }
    if (byte_offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(t3)\n", destination_register, byte_offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add t3, t3, t2\n"
                   "  ld %s, 0(t3)\n",
                   byte_offset,
                   destination_register) >= 0;
}

bool minic_riscv64_emit_incoming_parameter(FILE *file,
                           const MinicC0Program *program,
                           const MinicCoreFunction *function,
                           const MinicRiscv64CoreFrame *frame,
                           const MinicCoreInstruction *instruction) {
    size_t parameter_index;

    parameter_index = instruction->value.parameter_index;
    if (parameter_index >= function->parameter_count) {
        return false;
    }
    if (program != NULL) {
        MinicRiscv64AbiArgumentLocation location;

        if (!minic_riscv64_core_frame_parameter_location(frame, parameter_index, &location)) {
            return false;
        }
        if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
            location.floating_register_count != 0U &&
            (minic_type_is_float(instruction->type) ||
             minic_type_is_double(instruction->type))) {
            if (location.floating_register_count != 1U ||
                location.floating_register_begin >= 8U ||
                location.integer_register_count != 0U ||
                location.stack_slot_count != 0U) {
                return false;
            }
            if (minic_type_is_double(instruction->type)) {
                if (fprintf(file,
                            "  fmv.x.d t0, %s\n",
                            minic_riscv64_abi_floating_argument_registers[
                                location.floating_register_begin]) < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  fmv.x.w t0, %s\n"
                               "  slli t0, t0, 32\n"
                               "  srli t0, t0, 32\n",
                               minic_riscv64_abi_floating_argument_registers[
                                   location.floating_register_begin]) < 0) {
                return false;
            }
        } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR &&
                   (minic_type_is_long_double(instruction->type) ||
                    minic_type_is_int128_integer(instruction->type))) {
            const char *chunk_registers[2] = {"t0", "t1"};
            size_t chunk_index;

            if (location.value.storage_size != 16U || location.value.slot_count != 2U ||
                location.floating_register_count != 0U ||
                location.integer_register_count + location.stack_slot_count != 2U) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < 2U; ++chunk_index) {
                if (chunk_index < location.integer_register_count) {
                    size_t register_index = location.integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        fprintf(file, "  mv %s, %s\n",
                                chunk_registers[chunk_index],
                                minic_riscv64_abi_integer_argument_registers[register_index]) < 0) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location.integer_register_count;
                    if (stack_chunk >= location.stack_slot_count ||
                        !emit_incoming_stack_load64(
                            file, frame, chunk_registers[chunk_index],
                            location.stack_slot_begin + stack_chunk)) {
                        return false;
                    }
                }
            }
            return minic_riscv64_frame_store_int128(
                file, frame, instruction->result, chunk_registers[0], chunk_registers[1]);
        } else if ((location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER ||
                    location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) &&
                   location.floating_register_count == 0U) {
            if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
                location.integer_register_begin < 8U) {
                if (fprintf(file,
                            "  mv t0, %s\n",
                            minic_riscv64_abi_integer_argument_registers[
                                location.integer_register_begin]) < 0) {
                    return false;
                }
            } else if (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U) {
                if (!emit_incoming_stack_load64(
                        file, frame, "t0", location.stack_slot_begin)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else if (parameter_index < 8U) {
        if (fprintf(file, "  mv t0, %s\n", minic_riscv64_abi_integer_argument_registers[parameter_index]) <
            0) {
            return false;
        }
    } else {
        size_t stack_slot;

        stack_slot = parameter_index - 8U;
        if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
            return false;
        }
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "t0")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

bool minic_riscv64_emit_incoming_parameter_object(FILE *file,
                                  const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  const MinicRiscv64CoreFrame *frame,
                                  const MinicCoreInstruction *instruction) {
    MinicRiscv64AbiArgumentLocation location;
    MinicCoreObjectId object_id;
    MinicType object_value_type;
    size_t object_offset;
    size_t chunk_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        return false;
    }
    object_id = instruction->value.parameter_object.object_id;
    if (!minic_riscv64_core_frame_parameter_location(
            frame, instruction->value.parameter_object.parameter_index, &location) ||
        object_id >= function->object_count ||
        !minic_type_unqualified(function->objects[object_id].type, &object_value_type) ||
        !minic_type_equal(
            object_value_type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !minic_riscv64_core_frame_object_offset(program, function, frame, object_id, &object_offset)) {
        return false;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        return location.value.slot_count == 0U && location.integer_register_count == 0U &&
               location.stack_slot_count == 0U;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t copied;

        if (location.value.storage_size == 0U || location.value.slot_count != 1U ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
            size_t register_index = location.integer_register_begin;

            if (register_index >= 8U ||
                fprintf(file,
                        "  mv t1, %s\n",
                        minic_riscv64_abi_integer_argument_registers[register_index]) < 0) {
                return false;
            }
        } else if (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U) {
            if (!emit_incoming_stack_load64(
                    file, frame, "t1", location.stack_slot_begin)) {
                return false;
            }
        } else {
            return false;
        }
        if (!minic_riscv64_frame_emit_address(file, frame, "t0", object_offset)) {
            return false;
        }
        copied = 0U;
        while (copied < location.value.storage_size) {
            size_t chunk = location.value.storage_size - copied;
            size_t offset;

            if (chunk > 2048U) {
                chunk = 2048U;
            }
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(file,
                            "  lbu t2, %zu(t1)\n"
                            "  sb t2, %zu(t0)\n",
                            offset,
                            offset) < 0) {
                    return false;
                }
            }
            copied += chunk;
            if (copied < location.value.storage_size &&
                fprintf(file,
                        "  addi t0, t0, 2047\n"
                        "  addi t0, t0, 1\n"
                        "  addi t1, t1, 2047\n"
                        "  addi t1, t1, 1\n") < 0) {
                return false;
            }
        }
        return true;
    }
    if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count) {
        return false;
    }
    for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
        const char *source_register;
        size_t chunk_offset;
        size_t chunk_size;

        source_register = "t0";
        if (chunk_index < location.integer_register_count) {
            size_t register_index;

            register_index = location.integer_register_begin + chunk_index;
            if (register_index >= 8U) {
                return false;
            }
            source_register = minic_riscv64_abi_integer_argument_registers[register_index];
        } else {
            size_t stack_slot;

            stack_slot =
                location.stack_slot_begin + (chunk_index - location.integer_register_count);
            if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
                return false;
            }
        }
        chunk_offset = chunk_index * 8U;
        if (chunk_offset >= location.value.storage_size ||
            object_offset > SIZE_MAX - chunk_offset) {
            return false;
        }
        chunk_size = location.value.storage_size - chunk_offset;
        if (chunk_size > 8U) {
            chunk_size = 8U;
        }
        if (!minic_riscv64_frame_store_chunk(file, frame, source_register, object_offset + chunk_offset, chunk_size)) {
            return false;
        }
    }
    return true;
}

/* One RV64 psABI argument marshal for direct and indirect calls.
 * ABI cursor owns placement; this writer owns native transfer, including
 * 16-byte long-double splits, stack spills and partial record bytes.
 * Indirect-call unsupported shapes must remain fail-closed. */
static bool emit_core_call_argument(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreCallArgument *argument, MinicType argument_type,
    const MinicRiscv64AbiArgumentLocation *location,
    bool is_fixed_parameter, bool direct_call) {
    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        argument == NULL || location == NULL)
        return false;
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
        if (location->value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
            const char *chunk_registers[2] = {"t0", "t1"};
            size_t chunk_index;

            if ((!minic_type_is_long_double(argument_type) &&
                  !minic_type_is_int128_integer(argument_type)) ||
                location->value.storage_size != 16U || location->value.slot_count != 2U ||
                location->floating_register_count != 0U ||
                location->integer_register_count + location->stack_slot_count != 2U ||
                !minic_riscv64_frame_load_int128(file, frame, argument->value.value_id,
                                        chunk_registers[0], chunk_registers[1])) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < 2U; ++chunk_index) {
                if (chunk_index < location->integer_register_count) {
                    size_t register_index = location->integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        fprintf(file, "  mv %s, %s\n",
                                minic_riscv64_abi_integer_argument_registers[register_index],
                                chunk_registers[chunk_index]) < 0) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location->integer_register_count;
                    size_t stack_slot;
                    if (stack_chunk >= location->stack_slot_count ||
                        location->stack_slot_begin > SIZE_MAX - stack_chunk) {
                        return false;
                    }
                    stack_slot = location->stack_slot_begin + stack_chunk;
                    if (stack_slot > SIZE_MAX / 8U ||
                        !minic_riscv64_emit_sp_store64(
                            file, chunk_registers[chunk_index], stack_slot * 8U)) {
                        return false;
                    }
                }
            }
        } else if (location->floating_register_count != 0U) {
            const char *move_opcode;

            if (!is_fixed_parameter ||
                location->value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                (!minic_type_is_float(argument_type) &&
                 !minic_type_is_double(argument_type)) ||
                location->floating_register_count != 1U ||
                location->floating_register_begin >= 8U ||
                location->integer_register_count != 0U ||
                location->stack_slot_count != 0U ||
                !minic_riscv64_frame_load_value(file, frame, argument->value.value_id, "t0")) {
                return false;
            }
            move_opcode = minic_type_is_double(argument_type) ? "fmv.d.x" : "fmv.w.x";
            if (fprintf(file,
                        "  %s %s, t0\n",
                        move_opcode,
                        minic_riscv64_abi_floating_argument_registers[
                            location->floating_register_begin]) < 0) {
                return false;
            }
        } else if (location->integer_register_count == 1U &&
                   location->stack_slot_count == 0U) {
            if (location->integer_register_begin >= 8U ||
                !minic_riscv64_frame_load_value(
                    file,
                    frame,
                    argument->value.value_id,
                    minic_riscv64_abi_integer_argument_registers[location->integer_register_begin])) {
                return false;
            }
        } else if (location->integer_register_count == 0U &&
                   location->stack_slot_count == 1U) {
            size_t outgoing_offset;

            if (location->stack_slot_begin > SIZE_MAX / 8U ||
                !minic_riscv64_frame_load_value(file, frame, argument->value.value_id, "t0")) {
                return false;
            }
            outgoing_offset = location->stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                return false;
            }
        } else {
            return false;
        }
        return true;
    }
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
        size_t chunk_index;
        size_t object_offset;

        if ((!direct_call && !is_fixed_parameter) ||
            !minic_riscv64_core_frame_object_offset(
                program, function, frame, argument->value.object_id, &object_offset)) {
            return false;
        }
        if (location->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
            if (location->value.slot_count != 0U || location->integer_register_count != 0U ||
                location->stack_slot_count != 0U) {
                return false;
            }
            return true;
        }
        if (location->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (location->integer_register_count == 1U && location->stack_slot_count == 0U) {
                if (location->integer_register_begin >= 8U ||
                    !minic_riscv64_frame_emit_address(file,
                                        frame,
                                        minic_riscv64_abi_integer_argument_registers[
                                         location->integer_register_begin],
                                     object_offset)) {
                    return false;
                }
            } else if (location->integer_register_count == 0U &&
                       location->stack_slot_count == 1U) {
                size_t outgoing_offset;
                if (location->stack_slot_begin > SIZE_MAX / 8U ||
                    !minic_riscv64_frame_emit_address(file, frame, "t0", object_offset)) {
                    return false;
                }
                outgoing_offset = location->stack_slot_begin * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            } else {
                return false;
            }
            return true;
        }
        if (location->value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            return false;
        }
        for (chunk_index = 0U; chunk_index < location->value.slot_count; ++chunk_index) {
            size_t chunk_offset = chunk_index * 8U;
            size_t chunk_size;

            if (chunk_offset >= location->value.storage_size ||
                object_offset > SIZE_MAX - chunk_offset) {
                return false;
            }
            chunk_size = location->value.storage_size - chunk_offset;
            if (chunk_size > 8U) {
                chunk_size = 8U;
            }
            if (chunk_index < location->integer_register_count) {
                size_t register_index = location->integer_register_begin + chunk_index;
                if (register_index >= 8U ||
                    !minic_riscv64_frame_load_chunk(file, frame,
                                        minic_riscv64_abi_integer_argument_registers[register_index],
                                        object_offset + chunk_offset,
                                        chunk_size)) {
                    return false;
                }
            } else {
                size_t stack_chunk = chunk_index - location->integer_register_count;
                size_t stack_slot;
                size_t outgoing_offset;
                if (stack_chunk >= location->stack_slot_count ||
                    location->stack_slot_begin > SIZE_MAX - stack_chunk) {
                    return false;
                }
                stack_slot = location->stack_slot_begin + stack_chunk;
                if (stack_slot > SIZE_MAX / 8U ||
                    !minic_riscv64_frame_load_chunk(
                        file, frame, "t0", object_offset + chunk_offset, chunk_size)) {
                    return false;
                }
                outgoing_offset = stack_slot * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            }
        }
        return true;
    }
    return false;
}

/* The scalar return move is bit-for-bit identical for either callee
 * kind; record and binary128 returns keep their distinct ABI checks. */
static bool emit_core_call_scalar_result(
    FILE *file, const MinicC0Program *program,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction,
    const MinicRiscv64AbiValue *return_value) {
    if (file == NULL || program == NULL || frame == NULL ||
        instruction == NULL || return_value == NULL)
        return false;
    if (minic_type_is_float(instruction->type) ||
        minic_type_is_double(instruction->type)) {
        if (return_value->kind != MINIC_RISCV64_ABI_VALUE_FLOAT) {
            return false;
        }
        if (frame->named_parameter_cursor.lp64_soft_float)
            return minic_riscv64_frame_store_value(
                file, frame, instruction->result, "a0");
        if (minic_type_is_double(instruction->type)) {
            if (fprintf(file, "  fmv.x.d a0, fa0\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  fmv.x.w a0, fa0\n"
                           "  slli a0, a0, 32\n"
                           "  srli a0, a0, 32\n") < 0) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "a0");
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "a0");
}

/* RV64 psABI returns both 128-bit integers and binary128 long doubles
 * in a0/a1. The same physical return applies to direct and indirect CALL.
 * Never send an indirect wide result through the 64-bit scalar store: that
 * truncates the high XLEN word and can corrupt a following SSA slot. */
static bool emit_core_call_wide_result(
    FILE *file, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction,
    const MinicRiscv64AbiValue *return_value) {
    if (file == NULL || frame == NULL || instruction == NULL ||
        return_value == NULL ||
        (!minic_type_is_long_double(instruction->type) &&
         !minic_type_is_int128_integer(instruction->type)) ||
        return_value->kind != MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR ||
        return_value->storage_size != 16U || return_value->slot_count != 2U)
        return false;
    return minic_riscv64_frame_store_int128(
        file, frame, instruction->result, "a0", "a1");
}

/* One already-qualified RV64 psABI plan provides every CALL argument's
 * register/stack location. Machine emission does no second argument
 * classification, and both direct/indirect forms use the same transport. */
static bool emit_core_call_arguments(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreCallView *call, const MinicRiscv64PreparedCall *planned,
    bool direct_call) {
    size_t argument_index;
    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        call == NULL || planned == NULL ||
        call->argument_begin > function->call_argument_count ||
        call->argument_count > function->call_argument_count - call->argument_begin ||
        (call->argument_count != 0U && call->arguments == NULL) ||
        call->parameter_count > call->argument_count ||
        (call->parameter_count != 0U && call->parameter_types == NULL) ||
        planned->argument_count != call->argument_count)
        return false;
    for (argument_index = 0U; argument_index < call->argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &call->arguments[argument_index];
        MinicType actual_type, argument_type;
        const bool fixed = argument_index < call->parameter_count;
        if (!minic_core_call_view_argument_type(
                function, call, argument_index, &actual_type))
            return false;
        argument_type = fixed ? call->parameter_types[argument_index] : actual_type;
        if (!emit_core_call_argument(
                file, program, function, frame, argument, argument_type,
                &planned->arguments[argument_index], fixed, direct_call))
            return false;
    }
    return true;
}

/* The exact Common semantic CALL ordinal also indexes the RV64 psABI
 * locations built from that SAME ordered index. A single checked binary
 * lookup replaces two independent machine/emitter searches per CALL. */
static bool rv64_resolve_prepared_call(
    const MinicRiscv64CoreFrame *frame, size_t instruction_id,
    const MinicCoreCallView **view,
    const MinicRiscv64PreparedCall **planned) {
    size_t ordinal;
    if (frame == NULL || view == NULL || planned == NULL ||
        frame->semantic_calls == NULL)
        return false;
    *view = minic_core_call_index_find(
        frame->semantic_calls, instruction_id, &ordinal);
    if (*view == NULL ||
        frame->outgoing_calls.count != frame->semantic_calls->count ||
        ordinal >= frame->outgoing_calls.count ||
        frame->outgoing_calls.sites == NULL ||
        frame->outgoing_calls.sites[ordinal] == NULL ||
        frame->outgoing_calls.sites[ordinal]->instruction_id != instruction_id)
        return false;
    *planned = frame->outgoing_calls.sites[ordinal];
    return true;
}

/* Return-object storage is source/Core semantics. Both direct and indirect
 * calls consume the same psABI return classification and materialize exactly
 * the same GP-register chunks (or use hidden a0 for large records). */
static bool rv64_emit_call_record_result(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    MinicCoreObjectId object_id, const MinicRiscv64AbiValue *return_value) {
    size_t chunk_index;
    size_t object_offset;
    if (return_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        return return_value->storage_size > 16U &&
               return_value->slot_count == 1U && object_id < function->object_count;
    }
    if (return_value->kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        return_value->slot_count == 0U || return_value->slot_count > 2U ||
        object_id >= function->object_count ||
        !minic_riscv64_core_frame_object_offset(
            program, function, frame, object_id, &object_offset))
        return false;
    for (chunk_index = 0U; chunk_index < return_value->slot_count; ++chunk_index) {
        size_t chunk_offset = chunk_index * 8U;
        size_t chunk_size;
        const char *source_register =
            minic_riscv64_abi_integer_argument_registers[chunk_index];
        if (chunk_offset >= return_value->storage_size ||
            object_offset > SIZE_MAX - chunk_offset)
            return false;
        chunk_size = return_value->storage_size - chunk_offset;
        if (chunk_size > 8U) chunk_size = 8U;
        if (!minic_riscv64_frame_store_chunk(
                file, frame, source_register, object_offset + chunk_offset, chunk_size))
            return false;
    }
    return true;
}

static bool rv64_emit_call_hidden_return_pointer(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    MinicCoreObjectId object_id) {
    size_t result_offset;
    return object_id < function->object_count &&
           minic_riscv64_core_frame_object_offset(
               program, function, frame, object_id, &result_offset) &&
           minic_riscv64_frame_emit_address(file, frame, "a0", result_offset);
}

bool minic_riscv64_emit_direct_call(FILE *file,
                      const MinicC0Program *program,
                      const MinicCoreFunction *function,
                      const MinicRiscv64CoreFrame *frame,
                      const MinicCoreInstruction *instruction,
                      size_t instruction_id) {
    const MinicCoreCallView *call;
    const MinicCoreCallee *callee;
    MinicRiscv64AbiValue return_value;
    const MinicRiscv64PreparedCall *planned;
    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_CALL)
        return false;
    if (!rv64_resolve_prepared_call(frame, instruction_id, &call, &planned))
        return false;
    if (call == NULL || planned == NULL || call->callee == NULL ||
        planned->argument_count != call->argument_count)
        return false;
    callee = call->callee;
    return_value = planned->return_value;
    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        if (call->return_form != MINIC_CORE_CALL_RETURN_OBJECT ||
            !rv64_emit_call_hidden_return_pointer(file, program, function, frame,
                                                   instruction->value.call.result_object)) {
            return false;
        }
    }
    if (!emit_core_call_arguments(
            file, program, function, frame,
            call, planned, true))
        return false;
    if (fprintf(file, "  call %s\n", callee->name) < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_record(instruction->type)) {
        return rv64_emit_call_record_result(file, program, function, frame,
            instruction->value.call.result_object, &return_value);
    }
    if (minic_type_is_long_double(instruction->type) ||
        minic_type_is_int128_integer(instruction->type))
        return emit_core_call_wide_result(
            file, frame, instruction, &return_value);
    return emit_core_call_scalar_result(
        file, program, frame, instruction, &return_value);
}

bool minic_riscv64_emit_indirect_call(FILE *file,
                               const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction,
                               size_t instruction_id) {
    const MinicCoreCallView *call;
    MinicRiscv64AbiValue return_value;
    const MinicRiscv64PreparedCall *planned;
    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL)
        return false;
    if (!rv64_resolve_prepared_call(frame, instruction_id, &call, &planned))
        return false;
    if (call == NULL || planned == NULL || call->signature == NULL ||
        planned->argument_count != call->argument_count)
        return false;
    return_value = planned->return_value;
    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT &&
        (call->return_form != MINIC_CORE_CALL_RETURN_OBJECT ||
         !rv64_emit_call_hidden_return_pointer(file, program, function, frame,
             instruction->value.indirect_call.result_object)))
        return false;
    if (!emit_core_call_arguments(
            file, program, function, frame,
            call, planned, false))
        return false;
    if (!minic_riscv64_frame_load_value(file, frame, instruction->value.indirect_call.callee, "t0") ||
        fprintf(file, "  jalr ra, t0, 0\n") < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_record(instruction->type)) {
        return rv64_emit_call_record_result(file, program, function, frame,
            instruction->value.indirect_call.result_object, &return_value);
    }
    if (minic_type_is_long_double(instruction->type) ||
        minic_type_is_int128_integer(instruction->type))
        return emit_core_call_wide_result(
            file, frame, instruction, &return_value);
    return emit_core_call_scalar_result(
        file, program, frame, instruction, &return_value);
}

