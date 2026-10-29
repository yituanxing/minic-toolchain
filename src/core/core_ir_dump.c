#include "core/core_ir.h"

#include <inttypes.h>
#include <stdio.h>

/* The optional textual Core dump is a consumer, never an IR mutation owner.
 * Preserve pre-dump verification and the existing printed format verbatim. */
static bool dump_instruction(FILE *output,
                             const MinicCoreFunction *function,
                             const MinicCoreInstruction *instruction) {
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return fprintf(output,
                       "  %%%" PRIu32 " = const.int %" PRId64 "\n",
                       instruction->result,
                       instruction->value.integer_value) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (minic_type_is_long_double(instruction->type)) {
            return fprintf(output,
                           "  %%%" PRIu32 " = const.long_double.bits 0x%016" PRIx64 "%016" PRIx64 "\n",
                           instruction->result,
                           instruction->value.floating128_bits.high,
                           instruction->value.floating128_bits.low) >= 0;
        }
        return fprintf(output,
                       minic_type_is_float(instruction->type)
                           ? "  %%%" PRIu32 " = const.float.bits 0x%08" PRIx64 "\n"
                           : "  %%%" PRIu32 " = const.double.bits 0x%016" PRIx64 "\n",
                       instruction->result,
                       instruction->value.floating_bits) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
        return fprintf(output,
                       "  %%%" PRIu32 " = add.float %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
        return fprintf(output,
                       "  %%%" PRIu32 " = sub.float %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
        return fprintf(output,
                       "  %%%" PRIu32 " = mul.float %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        return fprintf(output,
                       "  %%%" PRIu32 " = div.float %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        return fprintf(output,
                       "  %%%" PRIu32 " = add.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        return fprintf(output,
                       "  %%%" PRIu32 " = sub.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
        return fprintf(output,
                       "  %%%" PRIu32 " = mul.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        return fprintf(output,
                       "  %%%" PRIu32 " = div.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = eq.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = lt.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = le.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        return fprintf(output,
                       "  %%%" PRIu32 " = add.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        return fprintf(output,
                       "  %%%" PRIu32 " = sub.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        return fprintf(output,
                       "  %%%" PRIu32 " = mul.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        return fprintf(output,
                       "  %%%" PRIu32 " = div.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        return fprintf(output,
                       "  %%%" PRIu32 " = rem.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        return fprintf(output,
                       "  %%%" PRIu32 " = and.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        return fprintf(output,
                       "  %%%" PRIu32 " = xor.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        return fprintf(output,
                       "  %%%" PRIu32 " = or.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        return fprintf(output,
                       "  %%%" PRIu32 " = shl.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        return fprintf(output,
                       "  %%%" PRIu32 " = shr.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = lt.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = lt.ptr %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = eq.scalar %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        const char *operator_name =
            instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD
                ? "add"
            : instruction->value.integer_overflow.operator_kind ==
                    MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT
                ? "sub"
                : "mul";
        return fprintf(output,
                       "  %%%" PRIu32 " = %s.overflow.int %%%" PRIu32 ", %%%" PRIu32 ", %%%" PRIu32
                       "\n",
                       instruction->result,
                       operator_name,
                       instruction->value.integer_overflow.left,
                       instruction->value.integer_overflow.right,
                       instruction->value.integer_overflow.result_address) >= 0;
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int-to-double %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.float-to-double %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.double-to-float %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.double-to-int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return fprintf(output,
                       "  %%%" PRIu32 " = bitcast.scalar %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        return fprintf(output,
                       "  %%%" PRIu32 " = neg.double %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        return fprintf(output,
                       "  %%%" PRIu32 " = ineg %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        return fprintf(output,
                       "  %%%" PRIu32 " = inot %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
        return fprintf(output,
                       "  %%%" PRIu32 " = clz.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return fprintf(output,
                       "  %%%" PRIu32 " = ctz.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return fprintf(output,
                       "  %%%" PRIu32 " = scalar.is_zero %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = call.frame.%s level=%u\n",
                       instruction->result,
                       instruction->value.call_frame_address.kind ==
                               MINIC_CORE_CALL_FRAME_ADDRESS_RETURN
                           ? "return"
                           : "frame",
                       instruction->value.call_frame_address.level) >= 0;
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = variadic.argument.address.%s\n",
                       instruction->result,
                       instruction->value.variadic_address.copy_destination
                           ? "copy-destination" : "start") >= 0;
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        return fprintf(output,
                       "  variadic.copy %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.variadic_copy.source,
                       instruction->value.variadic_copy.destination) >= 0;
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        return fprintf(output, "  %%%" PRIu32 " = variadic.next %%%" PRIu32 "\n",
                       instruction->result, instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return fprintf(output,
                       "  %%%" PRIu32 " = stack.alloc %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return fprintf(output,
                       "  %%%" PRIu32 " = parameter %zu\n",
                       instruction->result,
                       instruction->value.parameter_index) >= 0;
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return fprintf(output,
                       "  %%%" PRIu32 " = fixed.register.read binding=%zu\n",
                       instruction->result,
                       instruction->value.fixed_register_binding_id) >= 0;
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE:
        return fprintf(output,
                       "  fixed.register.write binding=%zu, %" PRIu32 "\n",
                       instruction->value.fixed_register_write.binding_id,
                       instruction->value.fixed_register_write.source) >= 0;
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return fprintf(output,
                       "  parameter.object %zu, %%o%" PRIu32 "\n",
                       instruction->value.parameter_object.parameter_index,
                       instruction->value.parameter_object.object_id) >= 0;
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = object.addr %%o%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.object_id) >= 0;
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = block.addr %%bb%" PRIu32 "\n",
                       instruction->result, instruction->value.block_id) >= 0;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (function == NULL || instruction->value.global_id >= function->global_count) {
            return false;
        }
        return fprintf(output,
                       "  %%%" PRIu32 " = global.addr @%s\n",
                       instruction->result,
                       function->globals[instruction->value.global_id].name) >= 0;
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (function == NULL ||
            instruction->value.function_symbol_id >= function->function_symbol_count) {
            return false;
        }
        return fprintf(output,
                       "  %%%" PRIu32 " = function.addr @%s\n",
                       instruction->result,
                       function->function_symbols[instruction->value.function_symbol_id].name) >= 0;
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = field.addr %%%" PRIu32 ", record=%zu, field=%zu\n",
                       instruction->result,
                       instruction->value.field_address.base,
                       instruction->value.field_address.record_id,
                       instruction->value.field_address.field_index) >= 0;
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return fprintf(output,
                       "  %%%" PRIu32 " = pointer.offset %%%" PRIu32 ", %%%" PRIu32
                       ", stride=%zu\n",
                       instruction->result,
                       instruction->value.pointer_offset.base,
                       instruction->value.pointer_offset.index,
                       instruction->value.pointer_offset.element_size) >= 0;
    case MINIC_CORE_INSTRUCTION_LOAD:
        return fprintf(output,
                       "  %%%" PRIu32 " = load%s %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.load.is_volatile ? ".volatile" : "",
                       instruction->value.load.address) >= 0;
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        return fprintf(output, "  %%%" PRIu32 " = atomic.load.relaxed %%%" PRIu32 "\n",
                       instruction->result, instruction->value.atomic_load.address) >= 0;
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        return fprintf(output, "  atomic.store.relaxed %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.atomic_store.stored_value,
                       instruction->value.atomic_store.address) >= 0;
    case MINIC_CORE_INSTRUCTION_STORE:
        return fprintf(output,
                       "  store%s %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.store.is_volatile ? ".volatile" : "",
                       instruction->value.store.stored_value,
                       instruction->value.store.address) >= 0;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return fprintf(output,
                       "  record.load%s %%%" PRIu32 ", %%o%" PRIu32 "\n",
                       instruction->value.record_load.is_volatile ? ".volatile" : "",
                       instruction->value.record_load.source_address,
                       instruction->value.record_load.destination_object) >= 0;
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return fprintf(output,
                       "  record.copy %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.record_copy.source_address,
                       instruction->value.record_copy.destination_address) >= 0;
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return fprintf(output,
                       "  asm.opaque id=%" PRIu32 "%s%s\n",
                       instruction->value.inline_asm_id,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return fprintf(output,
                       "  %%%" PRIu32 " = asm.register_output id=%" PRIu32 "%s%s\n",
                       instruction->result,
                       instruction->value.inline_asm_id,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreInlineAsmId inline_asm_id;

        inline_asm_id = instruction->value.register_output_input_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  %%%" PRIu32 " = asm.register_output_input id=%" PRIu32
                       " %%%" PRIu32 "%s%s\n",
                       instruction->result,
                       inline_asm_id,
                       instruction->value.register_output_input_inline_asm.operand,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreInlineAsmId inline_asm_id;
        size_t register_index;

        inline_asm_id = instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id;
        register_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        if (register_index == SIZE_MAX) {
            return fprintf(output,
                           "  asm.memory_rw_input id=%" PRIu32 " mem=%%%" PRIu32
                           " input=%%%" PRIu32 " operands=%zu,-,%zu%s%s\n",
                           inline_asm_id,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.operand,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index,
                           inline_asm->is_volatile ? " volatile" : "",
                           inline_asm->has_memory_clobber ? " memory" : "") >= 0;
        }
        return fprintf(output,
                       "  %%%" PRIu32 " = asm.memory_rw_input id=%" PRIu32
                       " mem=%%%" PRIu32 " input=%%%" PRIu32 " operands=%zu,%zu,%zu%s%s\n",
                       instruction->result,
                       inline_asm_id,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.operand,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index,
                       register_index,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreInlineAsmId inline_asm_id;

        inline_asm_id = instruction->value.scalar_input_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  asm.scalar_input id=%" PRIu32 " %%%" PRIu32 "%s%s\n",
                       inline_asm_id,
                       instruction->value.scalar_input_inline_asm.operand,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        MinicCoreInlineAsmId inline_asm_id;
        const MinicCoreInlineAsm *inline_asm;

        inline_asm_id = instruction->value.structured_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  asm.structured id=%" PRIu32 " operands=%zu%s%s\n",
                       inline_asm_id,
                       instruction->value.structured_inline_asm.operand_count,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return fprintf(output, "  compiler.barrier\n") >= 0;
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;

        if (function == NULL || instruction->value.call.callee_id >= function->callee_count) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (minic_type_is_record(callee->return_type)) {
            if (fprintf(output,
                        "  %%o%" PRIu32 " = call @",
                        instruction->value.call.result_object) < 0) {
                return false;
            }
        } else if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output, "  call @") < 0) {
                return false;
            }
        } else if (fprintf(output, "  %%%" PRIu32 " = call @", instruction->result) < 0) {
            return false;
        }
        if (fwrite(callee->name, 1U, callee->name_length, output) != callee->name_length ||
            fprintf(output, "(") < 0) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument =
                &function->call_arguments[instruction->value.call.argument_begin + argument_index];

            if (argument_index != 0U && fprintf(output, ", ") < 0) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (fprintf(output, "%%o%" PRIu32, argument->value.object_id) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return fprintf(output, ")\n") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        size_t argument_index;

        if (function == NULL ||
            instruction->value.indirect_call.signature_id >= function->call_signature_count ||
            instruction->value.indirect_call.callee >= function->value_count) {
            return false;
        }
        if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output,
                        "  call.indirect %%%" PRIu32 "(",
                        instruction->value.indirect_call.callee) < 0) {
                return false;
            }
        } else if (fprintf(output,
                           "  %%%" PRIu32 " = call.indirect %%%" PRIu32 "(",
                           instruction->result,
                           instruction->value.indirect_call.callee) < 0) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];

            if (argument_index != 0U && fprintf(output, ", ") < 0) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (fprintf(output, "%%o%" PRIu32, argument->value.object_id) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return fprintf(output,
                       ") signature=%" PRIu32 "\n",
                       instruction->value.indirect_call.signature_id) >= 0;
    }
    }
    return false;
}

static bool dump_terminator(FILE *output,
                            const MinicCoreFunction *function,
                            const MinicCoreTerminator *terminator) {
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (function != NULL && minic_type_is_record(function->return_type)) {
            return fprintf(output, "  return.object %%o%" PRIu32 "\n", terminator->return_object) >=
                   0;
        }
        if (terminator->return_value == MINIC_CORE_VALUE_INVALID) {
            return fprintf(output, "  return\n") >= 0;
        }
        return fprintf(output, "  return %%%" PRIu32 "\n", terminator->return_value) >= 0;
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return fprintf(output, "  unreachable\n") >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(output, "  br bb%" PRIu32 "\n", terminator->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return fprintf(output, "  indirect_br %%%" PRIu32 "\n",
                       terminator->indirect_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return fprintf(output,
                       "  cond_br %%%" PRIu32 ", bb%" PRIu32 ", bb%" PRIu32 "\n",
                       terminator->conditional.condition,
                       terminator->conditional.when_true,
                       terminator->conditional.when_false) >= 0;
    }
    return false;
}

bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {
    size_t block_index;
    size_t index;

    if (output == NULL || !minic_core_function_verify(function) ||
        fprintf(output, "core function @") < 0 ||
        fwrite(function->name, 1U, function->name_length, output) != function->name_length ||
        fprintf(output, "\n") < 0) {
        return false;
    }
    for (index = 0U; index < function->object_count; ++index) {
        if (fprintf(output,
                    "object %%o%" PRIu32 "%s\n",
                    (MinicCoreObjectId)index,
                    minic_type_is_volatile(function->objects[index].type) ? " volatile" : "") < 0) {
            return false;
        }
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block;

        block = &function->blocks[block_index];
        if (fprintf(output, "bb%zu:\n", block_index) < 0) {
            return false;
        }
        for (index = 0U; index < block->instruction_count; ++index) {
            const MinicCoreInstruction *instruction;

            instruction = &function->instructions[block->instructions[index]];
            if (!dump_instruction(output, function, instruction)) {
                return false;
            }
        }
        if (!dump_terminator(output, function, &block->terminator)) {
            return false;
        }
    }
    return true;
}
