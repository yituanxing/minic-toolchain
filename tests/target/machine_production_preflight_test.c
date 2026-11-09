/* Production admission must bind source identity and independently enforce
 * Core-effect coverage plus the complete physical Machine verifier. */
#include "target/common/machine_production_preflight.h"
#include "target/common/machine_builder.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "machine production preflight:%d: %s\n", \
            __LINE__, #expr); \
    return 1; } } while (0)

enum { PROBE_RETURN = 1U, PROBE_ACQUIRE = 2U };
enum { PROBE_SOURCE_FORM_ACQUIRE = 77U };

static bool probe_features(const MinicTargetInfo *target,
    uint64_t features, void *context) {
    (void)target;
    (void)context;
    return features == 0U;
}

static bool probe_abi(const MinicTargetInfo *target,
    MinicFunctionCallingConvention convention, void *context) {
    (void)target;
    (void)context;
    return convention == MINIC_CALLING_CONVENTION_DEFAULT;
}

static bool probe_frame(const MinicTargetInfo *target,
    const MinicMachineFunction *function,
    const MinicMachineFrameObject *frame, void *context) {
    (void)target;
    (void)function;
    (void)frame;
    (void)context;
    return true;
}

static bool probe_opcode(const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context) {
    (void)target;
    (void)context;
    if (opcode != PROBE_RETURN && opcode != PROBE_ACQUIRE) return false;
    memset(contract, 0, sizeof(*contract));
    if (opcode == PROBE_RETURN) {
        contract->required_effects = MINIC_MACHINE_EFFECT_RETURN;
        contract->permitted_effects = MINIC_MACHINE_EFFECT_RETURN;
        contract->terminates_block = true;
    } else {
        contract->required_effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
        contract->permitted_effects = contract->required_effects;
    }
    return true;
}

static bool probe_selected(const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)function;
    (void)context;
    return instruction->opcode == PROBE_RETURN ||
        instruction->opcode == PROBE_ACQUIRE;
}

static bool probe_selected_source_asm(
    const MinicMachineFunction *machine,
    const MinicMachineInstruction *selected, void *context) {
    (void)context;
    return machine != NULL && selected != NULL &&
        selected->opcode == PROBE_ACQUIRE &&
        selected->source_asm_machine_form_id == PROBE_SOURCE_FORM_ACQUIRE &&
        selected->source_asm_machine_form_variant == 4U &&
        selected->effects == (MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_VOLATILE |
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER);
}

static bool probe_core_source_asm(
    const MinicCoreFunction *core, const MinicCoreInstruction *instruction,
    const MinicCoreInlineAsm *source, const MinicMachineFunction *machine,
    const MinicMachineInstruction *selected, void *context) {
    (void)machine;
    (void)context;
    return core != NULL && instruction != NULL && source != NULL &&
        selected != NULL &&
        instruction->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM &&
        instruction->value.structured_inline_asm.operand_count == 2U &&
        instruction->value.structured_inline_asm.operands[0].kind ==
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
        instruction->value.structured_inline_asm.operands[1].kind ==
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT &&
        instruction->value.structured_inline_asm.operands[0].operand_index == 0U &&
        instruction->value.structured_inline_asm.operands[1].operand_index == 1U &&
        source->template_length == 7U &&
        memcmp(source->template_text, "ldar %0", 7U) == 0 &&
        source->register_clobber_count == 0U &&
        selected->has_core_origin && selected->core_instruction_id == 0U;
}

static bool probe_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t relocation, int64_t addend, void *context) {
    (void)target;
    (void)context;
    return symbol != NULL && relocation == 0U && addend == 0;
}

static bool probe_emit(FILE *stream, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    (void)function;
    (void)labels;
    (void)context;
    if (instruction->opcode == PROBE_RETURN)
        return fputs("\tret\n", stream) != EOF;
    return instruction->opcode == PROBE_ACQUIRE &&
        fputs("\tldar w0, [x1]\n", stream) != EOF;
}

static int check_selected_source_asm(void) {
    MinicCoreType signature_type = {0};
    MinicCoreTypeContext types = {0};
    MinicCoreInstruction core_ins = {0};
    MinicCoreInstructionId core_id = 0U;
    MinicCoreInlineAsm source_asm = {0};
    MinicCoreBlock core_block = {0};
    MinicCoreFunction core = {0};
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    MinicMachineGNUAsmPolicy machine_policy = {0};
    MinicMachineCoreCoveragePolicy coverage_policy = {0};
    MinicMachineProductionSource production = {0};
    bool reachable[] = {true};
    const unsigned int acquire_effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    size_t entry, body;
    MinicMachineInstruction *hardware;
    char name[] = "selected_asm_probe";

    signature_type.kind = MINIC_CORE_TYPE_FUNCTION;
    signature_type.value.function.calling_convention =
        MINIC_CALLING_CONVENTION_DEFAULT;
    types.types = &signature_type;
    types.type_count = 1U;
    core_ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    core_ins.value.structured_inline_asm.inline_asm_id = 0U;
    core_ins.value.structured_inline_asm.operand_count = 2U;
    core_ins.value.structured_inline_asm.operands[0].operand_index = 0U;
    core_ins.value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    core_ins.value.structured_inline_asm.operands[1].operand_index = 1U;
    core_ins.value.structured_inline_asm.operands[1].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    source_asm.template_text = "ldar %0";
    source_asm.template_length = 7U;
    source_asm.is_volatile = true;
    source_asm.has_memory_clobber = true;
    source_asm.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
    source_asm.target_machine_form_id = PROBE_SOURCE_FORM_ACQUIRE;
    source_asm.target_machine_form_variant = 4U;
    source_asm.source_asm_unique_id = 43U;
    core_block.instructions = &core_id;
    core_block.instruction_count = 1U;
    core_block.has_terminator = true;
    core_block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.name = name;
    core.name_length = strlen(name);
    core.type_context = &types;
    core.core_function_type_id = 0U;
    core.blocks = &core_block;
    core.block_count = 1U;
    core.instructions = &core_ins;
    core.instruction_count = 1U;
    core.inline_asms = &source_asm;
    core.inline_asm_count = 1U;
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = 8U;
    layout.pointer_alignment = 8U;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    target.data_layout = &layout;

    CHECK(minic_machine_function_initialize(&machine, &target, name, 0U));
    CHECK(minic_machine_builder_initialize(&builder, &machine));
    CHECK(minic_machine_builder_create_block(
        &builder, NULL, false, true, &entry));
    CHECK(minic_machine_builder_create_block(
        &builder, NULL, false, false, &body));
    CHECK(minic_machine_builder_position_at(&builder, entry));
    CHECK(minic_machine_builder_add_successor(&builder, body));
    CHECK(minic_machine_builder_set_fallthrough(&builder, body));
    CHECK(minic_machine_builder_position_at(&builder, body));
    CHECK(minic_machine_builder_mark_core_block(&builder, 0U));
    minic_machine_builder_set_origin(&builder, 0U, 0U);
    CHECK(minic_machine_builder_emit_selected(&builder, PROBE_ACQUIRE,
        acquire_effects, NULL, 0U, NULL, 0U));
    CHECK(minic_machine_builder_seal_selected_source_asm(
        &builder, PROBE_SOURCE_FORM_ACQUIRE, 4U, 43U));
    CHECK(!minic_machine_builder_seal_selected_source_asm(
        &builder, PROBE_SOURCE_FORM_ACQUIRE, 4U, 43U));
    hardware = &machine.blocks[body].instructions[0];
    minic_machine_builder_clear_origin(&builder);
    CHECK(minic_machine_builder_emit_selected(&builder, PROBE_RETURN,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U));

    machine_policy.verify.lookup_opcode = probe_opcode;
    machine_policy.verify.validate_selected_instruction = probe_selected;
    machine_policy.verify.validate_generated_features = probe_features;
    machine_policy.verify.validate_function_abi = probe_abi;
    machine_policy.verify.validate_frame_object = probe_frame;
    machine_policy.verify.validate_relocation = probe_relocation;
    machine_policy.verify.validate_selected_source_asm =
        probe_selected_source_asm;
    machine_policy.emit_selected = probe_emit;
    coverage_policy.prove_selected_source_asm = probe_core_source_asm;
    production.core = &core;
    production.reachable_blocks = reachable;
    production.target = &target;
    production.symbol = name;
    production.core_asm_policy = &coverage_policy;

    CHECK(minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    CHECK(!minic_machine_core_coverage_verify(&core, reachable, &machine));
    production.core_asm_policy = NULL;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    production.core_asm_policy = &coverage_policy;
    coverage_policy.prove_selected_source_asm = NULL;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    coverage_policy.prove_selected_source_asm = probe_core_source_asm;
    machine_policy.verify.validate_selected_source_asm = NULL;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    machine_policy.verify.validate_selected_source_asm =
        probe_selected_source_asm;
    hardware->source_asm_machine_form_variant = 8U;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    hardware->source_asm_machine_form_variant = 4U;
    hardware->source_asm_unique_id = 42U;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    hardware->source_asm_unique_id = 43U;
    hardware->effects &= ~(unsigned int)MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    hardware->effects = acquire_effects;
    source_asm.register_clobber_count = 1U;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    source_asm.register_clobber_count = 0U;
    source_asm.is_goto = true;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    source_asm.is_goto = false;
    core_ins.value.structured_inline_asm.operands[1].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    CHECK(!minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    core_ins.value.structured_inline_asm.operands[1].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    CHECK(minic_machine_production_preflight(
        &production, &machine, &machine_policy));
    {
        MinicMachineBlock *selected_block = &machine.blocks[body];
        MinicMachineInstruction *const original = selected_block->instructions;
        const size_t original_count = selected_block->instruction_count;
        const size_t original_capacity = selected_block->instruction_capacity;
        MinicMachineInstruction injected[3] = {0};
        /* Repeating a genuine selected instruction is not two proofs for
         * one source occurrence, even with the same exact form and ID. */
        injected[0] = original[0];
        injected[1] = original[0];
        injected[2] = original[1];
        selected_block->instructions = injected;
        selected_block->instruction_count = 3U;
        selected_block->instruction_capacity = 3U;
        CHECK(!minic_machine_core_coverage_verify_with_policy(
            &core, reachable, &machine, &coverage_policy));
        CHECK(!minic_machine_gnu_asm_preflight(&machine, &machine_policy));
        /* A detached Core barrier cannot falsely discharge GNU memory
         * clobber requirements for a hardware source-ASM expansion. */
        injected[0] = (MinicMachineInstruction){
            .kind = MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER,
            .effects = MINIC_MACHINE_EFFECT_COMPILER_BARRIER,
            .has_core_origin = true,
            .core_block_id = 0U,
            .core_instruction_id = 0U
        };
        injected[1] = original[0];
        CHECK(!minic_machine_core_coverage_verify_with_policy(
            &core, reachable, &machine, &coverage_policy));
        selected_block->instructions = original;
        selected_block->instruction_count = original_count;
        selected_block->instruction_capacity = original_capacity;
    }
    minic_machine_function_destroy(&machine);
    return 0;
}

int main(void) {
    MinicCoreType signature_type = {0};
    MinicCoreTypeContext types = {0};
    MinicCoreBlock core_block = {0};
    MinicCoreFunction core = {0};
    MinicTargetInfo target = {0};
    MinicTargetInfo foreign_target = {0};
    MinicDataLayout layout = {0};
    MinicMachineFunction machine = {0};
    MinicMachineBuilder builder = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineProductionSource source = {0};
    bool reachable[] = {true};
    size_t entry = SIZE_MAX, body = SIZE_MAX;
    char function_name[] = "production_probe";

    signature_type.kind = MINIC_CORE_TYPE_FUNCTION;
    signature_type.value.function.calling_convention =
        MINIC_CALLING_CONVENTION_DEFAULT;
    types.types = &signature_type;
    types.type_count = 1U;
    core.name = function_name;
    core.name_length = strlen(function_name);
    core.type_context = &types;
    core.core_function_type_id = 0U;
    core_block.has_terminator = true;
    core_block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.blocks = &core_block;
    core.block_count = 1U;
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = 8U;
    layout.pointer_alignment = 8U;
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    target.data_layout = &layout;
    foreign_target.architecture = MINIC_TARGET_ARCH_X86_64;
    foreign_target.data_layout = &layout;

    CHECK(minic_machine_function_initialize(
        &machine, &target, function_name, 0U));
    CHECK(minic_machine_builder_initialize(&builder, &machine));
    CHECK(minic_machine_builder_create_block(
        &builder, NULL, false, true, &entry));
    CHECK(minic_machine_builder_create_block(
        &builder, NULL, false, false, &body));
    CHECK(entry == 0U && body == 1U);
    CHECK(minic_machine_builder_position_at(&builder, entry));
    CHECK(minic_machine_builder_add_successor(&builder, body));
    CHECK(minic_machine_builder_set_fallthrough(&builder, body));
    CHECK(minic_machine_builder_position_at(&builder, body));
    CHECK(minic_machine_builder_emit_selected(&builder, 1U,
        MINIC_MACHINE_EFFECT_RETURN, NULL, 0U, NULL, 0U));
    machine.blocks[1].has_core_block_origin = true;
    machine.blocks[1].core_block_id = 0U;

    policy.verify.lookup_opcode = probe_opcode;
    policy.verify.validate_selected_instruction = probe_selected;
    policy.verify.validate_generated_features = probe_features;
    policy.verify.validate_function_abi = probe_abi;
    policy.verify.validate_frame_object = probe_frame;
    policy.verify.validate_relocation = probe_relocation;
    policy.emit_selected = probe_emit;
    source.core = &core;
    source.reachable_blocks = reachable;
    source.target = &target;
    source.symbol = function_name;

    CHECK(minic_machine_core_coverage_verify(&core, reachable, &machine));
    CHECK(minic_machine_gnu_asm_preflight(&machine, &policy));
    CHECK(minic_machine_production_preflight(&source, &machine, &policy));
    source.symbol = "another_symbol";
    /* The sealed Core identity and exported ELF symbol are independent:
     * a legitimate Module alias must not make physical admission fail. */
    CHECK(minic_machine_production_preflight(&source, &machine, &policy));
    source.symbol = "";
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    source.symbol = function_name;
    /* Conversely, a selector bound to another Core function is invalid.
     * The Core identity is counted and cannot be read via strcmp(). */
    core.name = "other_core_function";
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    core.name = function_name;
    ++core.name_length;
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    --core.name_length;
    {
        char *const selected_name = machine.name;
        machine.name = "other_selected_function";
        CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
        machine.name = selected_name;
    }
    source.target = &foreign_target;
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    source.target = &target;
    target.architecture = MINIC_TARGET_ARCH_AARCH64;
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    target.architecture = MINIC_TARGET_ARCH_X86_64;
    machine.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    machine.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    reachable[0] = false;
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    reachable[0] = true;
    policy.verify.lookup_opcode = NULL;
    CHECK(!minic_machine_production_preflight(&source, &machine, &policy));
    policy.verify.lookup_opcode = probe_opcode;
    CHECK(minic_machine_production_preflight(&source, &machine, &policy));
    /* GNU emission must reject generated names colliding with any exported,
     * alias or selected local marker, even when the MachineIR's source labels
     * themselves are otherwise lexically valid and individually unique. */
    {
        char *const saved = machine.blocks[entry].exported_label;
        machine.blocks[entry].exported_label =
            ".Lminic_mir_production_probe_1";
        CHECK(!minic_machine_gnu_asm_preflight(&machine, &policy));
        machine.blocks[entry].exported_label = saved;
        CHECK(minic_machine_gnu_asm_preflight(&machine, &policy));
    }
    {
        const MinicMachineInstruction local = {
            .kind = MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL,
            .local_label_name = ".Lminic_mir_production_probe_1"
        };
        CHECK(minic_machine_block_append_instruction(&machine, entry, &local));
        CHECK(!minic_machine_gnu_asm_preflight(&machine, &policy));
        CHECK(minic_machine_block_erase_instruction(&machine, entry, 0U));
        CHECK(minic_machine_gnu_asm_preflight(&machine, &policy));
    }
    CHECK(minic_machine_block_add_alias_label(&machine, entry,
        ".Ldistinct_external_alias"));
    CHECK(minic_machine_gnu_asm_preflight(&machine, &policy));
    CHECK(minic_machine_block_add_alias_label(&machine, entry,
        ".Lminic_mir_production_probe_1"));
    CHECK(!minic_machine_gnu_asm_preflight(&machine, &policy));
    {
        MinicMachineFunction one = {0};
        size_t only = SIZE_MAX;
        CHECK(minic_machine_function_initialize(&one, &target,
            "one_block_proof", 0U));
        CHECK(!minic_machine_gnu_asm_preflight(&one, &policy));
        CHECK(minic_machine_function_add_block(&one, NULL, false, &only));
        CHECK(only == 0U);
        {
            const MinicMachineInstruction ret = {
                .kind = MINIC_MACHINE_INSTRUCTION_SELECTED,
                .opcode = PROBE_RETURN,
                .effects = MINIC_MACHINE_EFFECT_RETURN
            };
            CHECK(minic_machine_block_append_instruction(&one, only, &ret));
        }
        CHECK(minic_machine_gnu_asm_preflight(&one, &policy));
        minic_machine_function_destroy(&one);
    }
    minic_machine_function_destroy(&machine);
    return check_selected_source_asm();
}
