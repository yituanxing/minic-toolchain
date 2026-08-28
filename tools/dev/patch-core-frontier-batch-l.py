#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

base = subprocess.run(
    [sys.executable, "tools/dev/patch-core-frontier-batch-l-base.py"],
    text=True,
    capture_output=True,
)
if base.stdout:
    print(base.stdout, end="")
expected = "core_codegen.c structured clobber-independent header: anchor count=2"
if base.returncode == 0:
    print("CORE_BATCH_L_BASE_COMPLETED")
    raise SystemExit(0)
if expected not in base.stderr:
    if base.stderr:
        print(base.stderr, file=sys.stderr, end="")
    raise SystemExit(base.returncode)
print("CORE_BATCH_L_CONTINUE_AFTER_KNOWN_AMBIGUOUS_ANCHOR")

path = Path("src/target/riscv64/core_codegen.c")
text = path.read_text()

def replace_in_section(text, marker, old, new, label):
    begin = text.find(marker)
    if begin < 0:
        raise SystemExit(f"{label}: section marker missing")
    tail = text[begin:]
    count = tail.count(old)
    if count != 1:
        raise SystemExit(f"{label}: section anchor count={count}")
    return text[:begin] + tail.replace(old, new, 1)

# The base patcher reaches its ambiguous validator anchor before writing the
# RV64 file. Re-apply the three edits that were still only in memory there.
if "#include <string.h>\n" not in text:
    if text.count("#include <stdio.h>\n") != 1:
        raise SystemExit("string header anchor mismatch")
    text = text.replace(
        "#include <stdio.h>\n",
        "#include <stdio.h>\n#include <string.h>\n",
        1,
    )

helper = r'''static bool core_inline_asm_clobbers_register(const MinicCoreInlineAsm *inline_asm,
                                               const char *register_name) {
    size_t index;
    size_t name_length;

    if (inline_asm == NULL || register_name == NULL) {
        return true;
    }
    name_length = strlen(register_name);
    for (index = 0U; index < inline_asm->register_clobber_count; ++index) {
        const MinicCoreInlineAsmRegisterClobber *clobber =
            &inline_asm->register_clobbers[index];
        if (clobber->name != NULL && clobber->name_length == name_length &&
            memcmp(clobber->name, register_name, name_length) == 0) {
            return true;
        }
    }
    return false;
}

'''
helper_anchor = "/* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: the Core model is generic.\n"
if helper not in text:
    if text.count(helper_anchor) != 1:
        raise SystemExit("clobber helper anchor mismatch")
    text = text.replace(helper_anchor, helper + helper_anchor, 1)

structured = "static bool core_structured_inline_asm_supported"
text = replace_in_section(
    text,
    structured,
    '''    size_t register_outputs = 0U;
    size_t memory_readwrites = 0U;
    size_t scalar_inputs = 0U;
''',
    '''    size_t register_outputs = 0U;
    size_t register_readwrites = 0U;
    size_t memory_readwrites = 0U;
    size_t scalar_inputs = 0U;
''',
    "structured readwrite counter",
)
text = replace_in_section(
    text,
    structured,
    '''    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || !inline_asm->has_memory_clobber) {
        return false;
    }
''',
    '''    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
''',
    "structured clobber-independent header",
)
text = replace_in_section(
    text,
    structured,
    '''        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            register_outputs += 1U;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    '''        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !core_scalar_type(value_type)) {
                return false;
            }
            if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
                register_readwrites += 1U;
            } else {
                register_outputs += 1U;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    "structured readwrite validator",
)
text = replace_in_section(
    text,
    structured,
    '''    if (!((register_outputs == 2U && memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count) ||
          (register_outputs == 0U && memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U))) {
        return false;
    }
''',
    '''    if (!((register_outputs == 2U && register_readwrites == 0U &&
           memory_readwrites == 1U && scalar_inputs <= 2U &&
           scalar_inputs + 3U == instruction->value.structured_inline_asm.operand_count &&
           inline_asm->has_memory_clobber) ||
          (register_outputs == 0U && register_readwrites == 0U &&
           memory_readwrites == 0U && scalar_inputs == 2U &&
           instruction->value.structured_inline_asm.operand_count == 2U &&
           inline_asm->has_memory_clobber) ||
          (register_outputs == 0U && register_readwrites == 1U &&
           memory_readwrites == 0U && scalar_inputs == 0U &&
           instruction->value.structured_inline_asm.operand_count == 1U &&
           !inline_asm->has_memory_clobber))) {
        return false;
    }
    if (register_readwrites == 1U &&
        core_inline_asm_clobbers_register(inline_asm, "t0") &&
        core_inline_asm_clobbers_register(inline_asm, "t1")) {
        return false;
    }
''',
    "structured accepted shapes",
)

emit = "static bool emit_structured_inline_asm"
text = replace_in_section(
    text,
    emit,
    '''        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            register_name = output_registers[output_index++];
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    '''        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE: {
            MinicType pointee;
            MinicType value_type;

            while (output_index < 2U &&
                   core_inline_asm_clobbers_register(
                       inline_asm, output_registers[output_index])) {
                output_index += 1U;
            }
            if (output_index >= 2U) {
                return false;
            }
            register_name = output_registers[output_index++];
            if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
                if (!load_core_value(file, frame, binding->value, "t5") ||
                    !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                    !minic_type_unqualified(pointee, &value_type) ||
                    !core_scalar_type(value_type) ||
                    !minic_riscv64_emit_scalar_load_for_program(
                        file, program, value_type, register_name, "t5")) {
                    return false;
                }
            }
            break;
        }
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
''',
    "structured emitter preload readwrite",
)
text = replace_in_section(
    text,
    emit,
    '''        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) {
            continue;
        }
''',
    '''        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            continue;
        }
''',
    "structured emitter poststore readwrite",
)
path.write_text(text)
print("CORE_BATCH_L_PATCHED structured +r immediate asm with opaque register clobbers")
