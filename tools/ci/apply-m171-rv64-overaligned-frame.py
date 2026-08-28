#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M171 overaligned frame {label}: expected 1 match, got {count}")
    text = text.replace(old, new, 1)


# Keep all Core object/value/call offsets relative to one stable target frame base.
# When an object requires >16-byte alignment, RV64 cannot assume the psABI entry SP
# already satisfies it, so remember entry SP and dynamically align the body SP.
replace_once(
    '''    size_t hidden_result_pointer_offset;
    size_t varargs_offset;
''',
    '''    size_t hidden_result_pointer_offset;
    size_t entry_sp_offset;
    size_t stack_alignment;
    size_t varargs_offset;
''',
    "frame-entry-sp-fields",
)
replace_once(
    '''    bool has_hidden_result_pointer;
    bool has_variadic_argument_address;
''',
    '''    bool has_hidden_result_pointer;
    bool has_dynamic_stack_alignment;
    bool has_variadic_argument_address;
''',
    "frame-dynamic-flag",
)

replace_once(
    '''    size_t required_size;
    size_t outgoing_argument_size;

    if (function == NULL || frame == NULL ||
        !core_call_outgoing_stack_size(program, function, &outgoing_argument_size)) {
        return false;
    }
    storage_size = outgoing_argument_size;
''',
    '''    size_t required_size;
    size_t outgoing_argument_size;
    size_t maximum_object_alignment;

    if (function == NULL || frame == NULL ||
        !core_call_outgoing_stack_size(program, function, &outgoing_argument_size)) {
        return false;
    }
    storage_size = outgoing_argument_size;
    maximum_object_alignment = 16U;
''',
    "frame-max-alignment-init",
)

replace_once(
    '''            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count) {
            return false;
        }
        object_size *= function->objects[object_index].element_count;
''',
    '''            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count) {
            return false;
        }
        if (object_alignment > maximum_object_alignment) {
            maximum_object_alignment = object_alignment;
        }
        object_size *= function->objects[object_index].element_count;
''',
    "frame-object-alignment",
)

replace_once(
    '''    frame->has_variadic_argument_address =
        core_function_uses_variadic_argument_address(function);
''',
    '''    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
    frame->entry_sp_offset = 0U;
    if (frame->has_dynamic_stack_alignment) {
        if (!align_up(storage_size, 8U, &frame->entry_sp_offset) ||
            frame->entry_sp_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->entry_sp_offset + 8U;
    }

    frame->has_variadic_argument_address =
        core_function_uses_variadic_argument_address(function);
''',
    "frame-entry-sp-storage",
)

replace_once(
    '''            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U || object_alignment > 16U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count ||
            !align_up(current_offset, object_alignment, &current_offset)) {
''',
    '''            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count ||
            !align_up(current_offset, object_alignment, &current_offset)) {
''',
    "object-offset-alignment",
)

replace_once(
    '''            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
''',
    '''            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U) {
            return false;
        }
''',
    "preflight-object-alignment",
)

# Incoming stack arguments are addressed relative to entry SP by the psABI.
# Fixed frames can retain the old frame_size+slot calculation; dynamically
# aligned frames must dereference the saved entry SP instead.
parameter_anchor = '''static bool emit_parameter(FILE *file,
                           const MinicC0Program *program,
'''
helper = '''static bool emit_incoming_stack_load64(FILE *file,
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
        return minic_riscv64_emit_sp_load64(
            file, destination_register, frame->frame_size + byte_offset);
    }
    if (!minic_riscv64_emit_sp_load64(file, "t3", frame->entry_sp_offset)) {
        return false;
    }
    if (byte_offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(t3)\\n", destination_register, byte_offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\\n"
                   "  add t3, t3, t2\\n"
                   "  ld %s, 0(t3)\\n",
                   byte_offset,
                   destination_register) >= 0;
}

''' + parameter_anchor
replace_once(parameter_anchor, helper, "incoming-stack-helper")

replace_once(
    '''        } else if (location.integer_register_count == 0U && location.stack_slot_count == 1U) {
            if (location.stack_slot_begin > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + location.stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
                return false;
            }
''',
    '''        } else if (location.integer_register_count == 0U && location.stack_slot_count == 1U) {
            if (!emit_incoming_stack_load64(
                    file, frame, "t0", location.stack_slot_begin)) {
                return false;
            }
''',
    "scalar-program-stack-parameter",
)
replace_once(
    '''        stack_slot = parameter_index - 8U;
        if (stack_slot > (SIZE_MAX - frame->frame_size) / 8U) {
            return false;
        }
        incoming_offset = frame->frame_size + stack_slot * 8U;
        if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
            return false;
        }
''',
    '''        stack_slot = parameter_index - 8U;
        if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
            return false;
        }
''',
    "scalar-basic-stack-parameter",
)

replace_once(
    '''            stack_slot =
                location.stack_slot_begin + (chunk_index - location.integer_register_count);
            if (stack_slot > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + stack_slot * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
                return false;
            }
''',
    '''            stack_slot =
                location.stack_slot_begin + (chunk_index - location.integer_register_count);
            if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
                return false;
            }
''',
    "aggregate-stack-parameter",
)

replace_once(
    '''            if (location.stack_slot_begin > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + location.stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t1", incoming_offset)) {
                return false;
            }
''',
    '''            if (!emit_incoming_stack_load64(
                    file, frame, "t1", location.stack_slot_begin)) {
                return false;
            }
''',
    "indirect-object-stack-parameter",
)

# The local variables above are no longer needed once all incoming stack loads
# route through the single entry-SP-aware helper.
replace_once(
    '''    size_t parameter_index;
    size_t incoming_offset;

    parameter_index = instruction->value.parameter_index;
''',
    '''    size_t parameter_index;

    parameter_index = instruction->value.parameter_index;
''',
    "scalar-unused-incoming-offset",
)
replace_once(
    '''            size_t stack_slot;
            size_t incoming_offset;

            stack_slot =
''',
    '''            size_t stack_slot;

            stack_slot =
''',
    "aggregate-unused-incoming-offset",
)
replace_once(
    '''        } else if (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U) {
            size_t incoming_offset;

            if (!emit_incoming_stack_load64(
''',
    '''        } else if (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U) {
            if (!emit_incoming_stack_load64(
''',
    "indirect-unused-incoming-offset",
)

# For the over-aligned case, capture entry SP before allocation, align the body
# SP downward, then keep it fixed for objects, values, varargs and outgoing calls.
replace_once(
    '''    symbol_name = symbol->symbol_name;
    if (!minic_riscv64_emit_function_symbol_begin(file, symbol) ||
        !minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return false;
    }
''',
    '''    symbol_name = symbol->symbol_name;
    if (!minic_riscv64_emit_function_symbol_begin(file, symbol)) {
        return false;
    }
    if (frame.has_dynamic_stack_alignment) {
        if (fprintf(file, "  mv t0, sp\\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.frame_size) ||
            fprintf(file,
                    "  li t1, -%zu\\n"
                    "  and sp, sp, t1\\n",
                    frame.stack_alignment) < 0 ||
            !minic_riscv64_emit_sp_store64(file, "t0", frame.entry_sp_offset)) {
            return false;
        }
    } else if (!minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return false;
    }
''',
    "dynamic-prologue",
)

replace_once(
    '''    if (!minic_riscv64_emit_stack_release(file, frame.frame_size) || fprintf(file, "  ret\\n") < 0 ||
        !minic_riscv64_emit_function_symbol_end(file, symbol)) {
        return false;
    }
''',
    '''    if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\\n") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_stack_release(file, frame.frame_size)) {
        return false;
    }
    if (fprintf(file, "  ret\\n") < 0 ||
        !minic_riscv64_emit_function_symbol_end(file, symbol)) {
        return false;
    }
''',
    "dynamic-epilogue",
)

PATH.write_text(text)
print("M171_OVERALIGNED_FRAME_APPLIED")
