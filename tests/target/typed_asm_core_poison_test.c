#define _POSIX_C_SOURCE 200809L
/* AArch64/x86-64 GNU ASM source qualification may depend on exact C ranks,
 * but Core machine emission has no C descriptor to reopen. Check stable
 * positive emissions and reject malformed sealed Core identities. */
#include "core/core_ir.h"
#include "target/aarch64/asm_contract.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/x86_64/asm_contract.h"
#include "aarch64/typed_source_asm_probe.h"
#include "x86_64/typed_source_asm_probe.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    FILE *stream;
    char *buffer = NULL;
    size_t length = 0U;
    bool result;
    if (!output || capacity == 0U) return false;
    output[0] = '\0';
    stream = open_memstream(&buffer, &length);
    assert(stream != NULL);
    result = machine == POISON_AARCH64
        ? minic_test_aarch64_typed_source_asm_emit(stream, function,
              instruction)
        : minic_test_x86_typed_source_asm_emit(stream, function,
              instruction);
    assert(fflush(stream) == 0);
    assert(length < capacity);
    memcpy(output, buffer, length);
    output[length] = '\0';
    assert(fclose(stream) == 0);
    free(buffer);
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

    function.core_function_type_id = MINIC_CORE_TYPE_INVALID;
    assert(minic_core_function_define_signature(&function, &types,
        void_id, NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false));

    function.type_context = &types;
    function.values = value;
    function.value_count = 1U;
    function.inline_asms = &inline_asm;
    function.inline_asm_count = 1U;
    value[0].core_type_id = pointer_u64;

    inline_asm.template_text = (char *)template_text;
    inline_asm.template_length = strlen(template_text);
    inline_asm.is_volatile = true;
    inline_asm.source_asm_unique_id = 46U;
    inline_asm.goto_target = MINIC_CORE_BLOCK_INVALID;
    inline_asm.goto_second_target = MINIC_CORE_BLOCK_INVALID;
    /* Both hardware instructions require exact source-owned machine IDs.
     * The x86 Linux crash-register family captures RBX through a qualified
     * =m output, not a generic ID0 raw-text emission exception. */
    inline_asm.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
    inline_asm.target_machine_form_id = machine == POISON_AARCH64
        ? AARCH64_MACHINE_ASM_MRS : MINIC_X86_MACHINE_ASM_CRASH_REGISTER;
    inline_asm.target_machine_form_variant = machine == POISON_AARCH64
        ? 0U : 1U;
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
    if (getenv("MINIC_TYPED_SOURCE_PROBE_DEBUG") != NULL)
        fprintf(stderr, "CORE_POISON_POSITIVE machine=%d asm=%s\n",
                (int)machine, normal);
    assert(strstr(normal, machine == POISON_AARCH64
        ? "mrs x9, cntpct_el0\n" : "movq %rbx, (%r11)\n") != NULL);

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
    inline_asm.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
    if (machine == POISON_X86_64) {
        /* Identical source text must fail when stripped of its proven x86
         * physical-register source form: no generic ID0 fallback. */
        inline_asm.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
        inline_asm.target_machine_form_id = 0U;
        inline_asm.target_machine_form_variant = 0U;
        assert(!emit_one(machine, &function, &instruction,
                         rejected, sizeof(rejected)));
        assert(rejected[0] == '\0');
        inline_asm.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        inline_asm.target_machine_form_id = MINIC_X86_MACHINE_ASM_CRASH_REGISTER;
        inline_asm.target_machine_form_variant = 1U;
    }

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
