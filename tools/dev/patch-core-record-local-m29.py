#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M29 {label} anchor count={count}, expected 1")
    return text.replace(old, new, 1)


lower_path = Path("src/core/core_lower.c")
lower = lower_path.read_text()
lower = replace_once(
    lower,
    "    if (local->is_array || local->is_register_storage || !core_memory_scalar_type(local->type)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n",
    "    if (local->is_array || local->is_register_storage ||\n        (!core_memory_scalar_type(local->type) && !minic_type_is_record(local->type))) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n",
    "record local admission",
)
lower_path.write_text(lower)

codegen_path = Path("src/target/riscv64/core_codegen.c")
codegen = codegen_path.read_text()
codegen = replace_once(
    codegen,
    '''typedef struct MinicRiscv64CoreFrame {
    size_t frame_size;
    size_t object_count;
    size_t value_count;
    size_t return_address_offset;
    bool saves_return_address;
} MinicRiscv64CoreFrame;
''',
    '''typedef struct MinicRiscv64CoreFrame {
    size_t frame_size;
    size_t object_count;
    size_t value_count;
    size_t value_base_offset;
    size_t return_address_offset;
    bool saves_return_address;
} MinicRiscv64CoreFrame;
''',
    "frame fields",
)
old_frame = '''static bool core_frame_initialize(const MinicCoreFunction *function, MinicRiscv64CoreFrame *frame) {
    size_t slot_count;
    size_t storage_size;

    if (function == NULL || frame == NULL ||
        function->object_count > SIZE_MAX - function->value_count) {
        return false;
    }
    slot_count = function->object_count + function->value_count;
    frame->saves_return_address = core_function_has_call(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (slot_count > SIZE_MAX / 8U) {
            return false;
        }
        frame->return_address_offset = slot_count * 8U;
        if (slot_count == SIZE_MAX) {
            return false;
        }
        slot_count += 1U;
    }
    if (slot_count > SIZE_MAX / 8U) {
        return false;
    }
    storage_size = slot_count * 8U;
    if (!align_up(storage_size, 16U, &frame->frame_size)) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    return true;
}

static bool core_object_offset(const MinicRiscv64CoreFrame *frame,
                               MinicCoreObjectId object_id,
                               size_t *offset) {
    if (frame == NULL || offset == NULL || object_id >= frame->object_count) {
        return false;
    }
    *offset = (size_t)object_id * 8U;
    return true;
}

static bool
core_value_offset(const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id, size_t *offset) {
    size_t slot_index;

    if (frame == NULL || offset == NULL || value_id >= frame->value_count ||
        frame->object_count > SIZE_MAX - (size_t)value_id) {
        return false;
    }
    slot_index = frame->object_count + (size_t)value_id;
    if (slot_index > SIZE_MAX / 8U) {
        return false;
    }
    *offset = slot_index * 8U;
    return true;
}
'''
new_frame = '''static bool core_frame_initialize(const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  MinicRiscv64CoreFrame *frame) {
    size_t object_index;
    size_t storage_size;

    if (function == NULL || frame == NULL) {
        return false;
    }
    storage_size = 0U;
    for (object_index = 0U; object_index < function->object_count; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            !align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;
    }
    if (!align_up(storage_size, 8U, &frame->value_base_offset) ||
        function->value_count > (SIZE_MAX - frame->value_base_offset) / 8U) {
        return false;
    }
    storage_size = frame->value_base_offset + function->value_count * 8U;
    frame->saves_return_address = core_function_has_call(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }
    if (!align_up(storage_size, 16U, &frame->frame_size)) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    return true;
}

static bool core_object_offset(const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               MinicCoreObjectId object_id,
                               size_t *offset) {
    size_t current_offset;
    size_t object_index;

    if (function == NULL || offset == NULL || object_id >= function->object_count) {
        return false;
    }
    current_offset = 0U;
    for (object_index = 0U; object_index <= (size_t)object_id; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U ||
            !align_up(current_offset, object_alignment, &current_offset)) {
            return false;
        }
        if (object_index == (size_t)object_id) {
            *offset = current_offset;
            return true;
        }
        if (current_offset > SIZE_MAX - object_size) {
            return false;
        }
        current_offset += object_size;
    }
    return false;
}

static bool
core_value_offset(const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id, size_t *offset) {
    if (frame == NULL || offset == NULL || value_id >= frame->value_count ||
        (size_t)value_id > (SIZE_MAX - frame->value_base_offset) / 8U) {
        return false;
    }
    *offset = frame->value_base_offset + (size_t)value_id * 8U;
    return true;
}
'''
codegen = replace_once(codegen, old_frame, new_frame, "frame layout")
codegen = replace_once(
    codegen,
    '''    for (index = 0U; index < function->object_count; ++index) {
        if (!core_scalar_type(function->objects[index].type)) {
            return false;
        }
    }
''',
    '''    for (index = 0U; index < function->object_count; ++index) {
        size_t object_size;
        size_t object_alignment;
        MinicType object_type;

        object_type = function->objects[index].type;
        if ((!core_scalar_type(object_type) && !minic_type_is_record(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            object_size == 0U || object_alignment == 0U || object_alignment > 16U) {
            return false;
        }
    }
''',
    "object support",
)
codegen = replace_once(
    codegen,
    '''    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!core_object_offset(frame, instruction->value.object_id, &object_offset) ||
            !emit_sp_address(file, "t0", object_offset)) {
''',
    '''    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!core_object_offset(program, function, instruction->value.object_id, &object_offset) ||
            !emit_sp_address(file, "t0", object_offset)) {
''',
    "object address",
)
codegen = replace_once(
    codegen,
    "        !core_frame_initialize(function, &frame)) {\n",
    "        !core_frame_initialize(program, function, &frame)) {\n",
    "frame initialize call",
)
codegen_path.write_text(codegen)
print("M29_PATCH_APPLIED")
