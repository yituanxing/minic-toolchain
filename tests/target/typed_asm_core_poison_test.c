/* AArch64/x86-64 GNU ASM source qualification may depend on exact C ranks,
 * but Core machine emission has no C descriptor to reopen. Check stable
 * positive emissions and reject malformed sealed Core identities. */
#include "core/core_ir.h"
#include "target/aarch64/asm_contract.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/x86_64/machine_internal.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef enum PoisonMachine { POISON_AARCH64, POISON_X86_64 } PoisonMachine;

static bool register_integer(MinicCoreTypeContext *types, size_t bytes,
                             bool sign, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = bytes;
    type.value.integer.bit_width = (unsigned int)(bytes * 8U);
    type.value.integer.is_signed = sign;
    return minic_core_type_register(types, &type, id);
}

static bool register_pointer(MinicCoreTypeContext *types,
                             MinicCoreTypeId pointee, MinicCoreTypeId *id) {
    MinicCoreType type = {0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.pointer.pointee = pointee;
    return minic_core_type_register(types, &type, id);
}

/* The output buffers remain untouched for false admissions. A repeated
 * invocation with the same sealed Core IDs must emit exactly the same ISA. */
static bool emit_one(PoisonMachine machine, const MinicCoreFunction *function,
                     const MinicCoreInstruction *instruction,
                     char *output, size_t capacity) {
    MinicDirectLP64BlockContext a64 = {0};
    MinicLP64FramePlan a64_frame = {0};
    MinicMachineLP64FrameContract a64_machine_frame = {0};
    MinicX86Frame x86_frame = {0};
    MinicCoreModule module = { .finalized = true };
    MinicTargetInfo target = {0};
    size_t offsets[1] = {16U};
    FILE *stream;
    bool result;
    size_t size;
    if (!output || capacity == 0U) return false;
    output[0] = '\0';
    stream = tmpfile();
    assert(stream != NULL);
    a64_frame.value_count = 1U;
    a64_frame.value_offsets = offsets;
    a64.fn = function;
    a64.frame_plan = &a64_frame;
    a64_machine_frame.frame_size = 64U;
    a64.machine_frame = &a64_machine_frame;
    x86_frame.value_count = 1U;
    x86_frame.value_offsets = offsets;
    result = machine == POISON_AARCH64
        ? minic_aarch64_emit_qualified_structured_asm(stream, &a64, instruction)
        : minic_x86_64_emit_structured_asm(stream, &module, &target,
              function, &x86_frame, instruction, 0U);
    assert(fflush(stream) == 0);
    assert(fseek(stream, 0L, SEEK_SET) == 0);
    size = fread(output, 1U, capacity - 1U, stream);
    assert(!ferror(stream));
    assert(size < capacity - 1U || fgetc(stream) == EOF);
    output[size] = '\0';
    assert(fclose(stream) == 0);
    return result;
}

static void machine_core_poison(PoisonMachine machine) {
    MinicCoreTypeContext types;
    MinicCoreType none = {0};
    MinicCoreTypeId void_id, u32, u64, pointer_u32, pointer_u64;
    MinicCoreValue value[1] = {0};
    MinicCoreFunction function = {0};
    MinicCoreInlineAsm inline_asm = {0};
    MinicCoreInstruction instruction = {0};
    char normal[1024], poisoned[1024], rejected[1024];
    const char *const template_text = machine == POISON_AARCH64
        ? "mrs %0, cntpct_el0" : "movq %%rbx, %0";

    minic_core_type_context_initialize(&types);
    none.kind = MINIC_CORE_TYPE_VOID;
    assert(minic_core_type_register(&types, &none, &void_id));
    assert(register_integer(&types, 4U, false, &u32));
    assert(register_integer(&types, 8U, false, &u64));
    assert(register_pointer(&types, u32, &pointer_u32));
    assert(register_pointer(&types, u64, &pointer_u64));

    function.type_context = &types;
    function.values = value;
    function.value_count = 1U;
    function.inline_asms = &inline_asm;
    function.inline_asm_count = 1U;
    value[0].core_type_id = pointer_u64;

    inline_asm.template_text = (char *)template_text;
    inline_asm.template_length = strlen(template_text);
    inline_asm.is_volatile = true;
    /* P45 closes the source-qualified-machine handshake. The AArch64 MRS
     * hardware instruction must carry its exact target-owned machine ID,
     * while x86's longstanding MOV memory-output probe exercises the
     * deliberately independent, legal generic-structured ID0 path. */
    inline_asm.operand_plan_owner = machine == POISON_AARCH64
        ? MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED
        : MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
    inline_asm.target_machine_form_id = machine == POISON_AARCH64
        ? AARCH64_MACHINE_ASM_MRS : 0U;
    inline_asm.target_machine_form_variant = 0U;
    instruction.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    instruction.core_type_id = void_id;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.structured_inline_asm.inline_asm_id = 0U;
    instruction.value.structured_inline_asm.operand_count = 1U;
    instruction.value.structured_inline_asm.operands[0].operand_index = 0U;
    instruction.value.structured_inline_asm.operands[0].value = 0U;
    instruction.value.structured_inline_asm.operands[0].kind =
        machine == POISON_AARCH64
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;

    assert(emit_one(machine, &function, &instruction, normal, sizeof(normal)));
    assert(strstr(normal, machine == POISON_AARCH64
        ? "  mrs x9, cntpct_el0\n" : "  movq %rbx, (%r11)\n") != NULL);

    /* The public instruction/function schemas contain no C descriptor at all;
     * repeat with unchanged sealed Core identities and compare the output. */
    if (!emit_one(machine, &function, &instruction, poisoned,
                  sizeof(poisoned))) {
        fprintf(stderr, "Sealed Core re-emission rejected by %s machine emitter\n",
                machine == POISON_AARCH64 ? "AArch64" : "x86-64");
        assert(0 && "sealed Core identities must govern machine emission");
    }
    assert(strcmp(normal, poisoned) == 0);

    /* An unowned Core record is not a source qualification, even when its
     * template happens to spell a supported architectural instruction. */
    inline_asm.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_NONE;
    assert(!emit_one(machine, &function, &instruction, rejected,
                     sizeof(rejected)));
    assert(rejected[0] == '\0');
    inline_asm.operand_plan_owner = machine == POISON_AARCH64
        ? MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED
        : MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;

    /* A wrong Core pointee or a nonpointer carrier may not emit even one
     * instruction; an absent type arena is never a C fallback invitation. */
    value[0].core_type_id = pointer_u32;
    assert(!emit_one(machine, &function, &instruction, rejected,
                     sizeof(rejected)));
    assert(rejected[0] == '\0');
    value[0].core_type_id = u64;
    assert(!emit_one(machine, &function, &instruction, rejected,
                     sizeof(rejected)));
    assert(rejected[0] == '\0');
    value[0].core_type_id = pointer_u64;
    function.type_context = NULL;
    assert(!emit_one(machine, &function, &instruction, rejected,
                     sizeof(rejected)));
    assert(rejected[0] == '\0');
    function.type_context = &types;
    instruction.value.structured_inline_asm.operands[0].value = 3U;
    assert(!emit_one(machine, &function, &instruction, rejected,
                     sizeof(rejected)));
    assert(rejected[0] == '\0');
    minic_core_type_context_destroy(&types);
}

int main(void) {
    machine_core_poison(POISON_AARCH64);
    machine_core_poison(POISON_X86_64);
    puts("TYPED_ASM_CORE_POISON=PASS targets=2 positive=2 sealed_core_stable=2 wrong_owner=2 wrong_core=4 null_context=2 invalid_ssa=2 partial_asm=0");
    return 0;
}
