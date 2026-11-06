#define _POSIX_C_SOURCE 200809L
/* A Core entry need not be block zero. The prologue may only reach the
 * declared entry, and a Core CFG backedge must never re-enter the prologue. */
#include "target/loongarch64/machine_select.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/common/machine_gnu_asm.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_aggregate.h"
#include "target/data_layout.h"
#include "test_tempfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool test_nonzero_core_entry(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type;
    MinicCoreBlockId before, entry;
    MinicCoreTerminator term = {0};
    const bool reachable[2] = {true, true};
    FILE *output = NULL;
    char text[2048];
    size_t n;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_entry_nonzero"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_entry_nonzero", sizeof("loong_entry_nonzero"));
    core.name_length = sizeof("loong_entry_nonzero") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_type)) goto done;
    stage = "signature";
    if (!minic_core_function_define_signature(&core, &types,
            void_type, NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false))
        goto done;
    stage = "blocks";
    if (!minic_core_function_add_block(&core, &before) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_BRANCH;
    term.branch_target = entry;
    stage = "terminators";
    if (!minic_core_function_set_terminator(&core, before, &term)) goto done;
    term.branch_target = before;
    /* Production Core verifier currently mandates entry_block == 0. First
     * seal a verified function, then deliberately vary this single field to
     * exercise the target selector's declared-entry mapping. The variant is
     * a target-only synthetic probe, not a CoreVerifier-admitted function. */
    if (!minic_core_function_set_terminator(&core, entry, &term)) goto done;
    stage = "attach";
    if (!minic_core_function_attach_type_context(&core, &types)) goto done;
    core.entry_block = entry;
    stage = "selector";
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .name = core.name
    };
    if (!minic_loongarch64_select_machine(&machine, &ctx) ||
        machine.block_count != 3U ||
        !machine.blocks[0].machine_auxiliary ||
        machine.blocks[1].machine_auxiliary ||
        machine.blocks[2].machine_auxiliary ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serialize";
    output = loong_test_tempfile();
    if (output == NULL ||
        !minic_machine_gnu_asm_emit_body(output, &machine, &policy) ||
        fflush(output) != 0 || fseek(output, 0L, SEEK_SET) != 0) goto done;
    n = fread(text, 1U, sizeof(text) - 1U, output);
    text[n] = '\0';
    stage = "labels";
    ok = strstr(text, "loong_entry_nonzero:\n") == NULL &&
         strstr(text, "  b .Lminic_mir_loong_entry_nonzero_2\n") != NULL &&
         strstr(text, ".Lminic_mir_loong_entry_nonzero_1:\n") != NULL &&
         strstr(text, ".Lminic_mir_loong_entry_nonzero_2:\n") != NULL &&
         strstr(text, "  b loong_entry_nonzero\n") == NULL;
done:
    if (!ok) fprintf(stderr, "LOONG_CORE_MIR_NONZERO_ENTRY=FAIL stage=%s\n",
                     stage);
    if (output != NULL) fclose(output);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* These are Core storage effects, not snippets of target assembly. Prove
 * their exact read/write provenance and that the 32-byte counted copy is
 * represented by real machine CFG, including independent volatile sides. */
static bool test_record_transfer(size_t bytes, unsigned int mode) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicCoreType t = {0};
    MinicCoreTypeId void_ty, byte_ty, object_ty, pointer_ty;
    MinicCoreTypeId formals[2];
    MinicCoreInstruction ins = {0};
    MinicCoreValueId first, second;
    MinicCoreBlockId entry;
    MinicCoreTerminator ret = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    FILE *assembly = NULL;
    char assembly_text[4096];
    size_t text_length;
    size_t value_offsets[2] = {0U, 8U};
    size_t object_offsets[1] = {32U};
    size_t effect_reads = 0U, effect_writes = 0U;
    size_t volatile_reads = 0U, volatile_writes = 0U, auxiliary = 0U;
    size_t i, j;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_record_transfer"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_record_transfer", sizeof("loong_record_transfer"));
    core.name_length = sizeof("loong_record_transfer") - 1U;
    t.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &t, &void_ty)) goto done;
    t = (MinicCoreType){0};
    t.kind = MINIC_CORE_TYPE_INTEGER;
    t.has_layout = true;
    t.size = 1U;
    t.alignment = 1U;
    t.value.integer.bit_width = 8U;
    if (!minic_core_type_register(&types, &t, &byte_ty)) goto done;
    t = (MinicCoreType){0};
    t.kind = mode == 2U ? MINIC_CORE_TYPE_VECTOR : MINIC_CORE_TYPE_RECORD;
    t.has_layout = true;
    t.size = bytes;
    t.alignment = mode == 2U ? 16U : 1U;
    if (mode == 2U) {
        t.value.vector.element = byte_ty;
        t.value.vector.element_count = 16U;
        t.value.vector.representation = MINIC_CORE_VECTOR_STORAGE_ONLY;
    } else t.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &t, &object_ty)) goto done;
    t = (MinicCoreType){0};
    t.kind = MINIC_CORE_TYPE_POINTER;
    t.has_layout = true;
    t.size = 8U;
    t.alignment = 8U;
    t.value.pointer.pointee = object_ty;
    if (!minic_core_type_register(&types, &t, &pointer_ty)) goto done;
    formals[0] = pointer_ty;
    formals[1] = pointer_ty;
    stage = "signature";
    if (!minic_core_function_define_signature(&core, &types, void_ty,
            formals, 2U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = pointer_ty;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&core, entry,
            &ins, &first)) goto done;
    ins.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(&core, entry,
            &ins, &second)) goto done;
    stage = "effect";
    ins = (MinicCoreInstruction){0};
    ins.core_type_id = object_ty;
    if (mode == 0U) {
        ins.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
        ins.value.record_copy.source_address = first;
        ins.value.record_copy.destination_address = second;
        ins.value.record_copy.source_volatile = true;
    } else if (mode == 1U) {
        ins.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
        ins.value.record_load.source_address = first;
        ins.value.record_load.destination_object = 0U;
        ins.value.record_load.is_volatile = true;
        core.objects = calloc(1U, sizeof(*core.objects));
        if (core.objects == NULL) goto done;
        core.objects[0].core_type_id = object_ty;
        core.object_count = core.object_capacity = 1U;
        plan.object_count = 1U;
        plan.object_offsets = object_offsets;
    } else {
        ins.kind = MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY;
        ins.value.vector_storage_copy.source_address = first;
        ins.value.vector_storage_copy.destination_address = second;
    }
    stage = "append-effect";
    if (!minic_core_function_append_effect_instruction(&core, entry,
            &ins)) goto done;
    ret.kind = MINIC_CORE_TERMINATOR_RETURN;
    ret.return_value = MINIC_CORE_VALUE_INVALID;
    stage = "seal";
    if (!minic_core_function_set_terminator(&core, entry, &ret) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    stage = "plan";
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 128U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 2U;
    plan.value_offsets = value_offsets;
    incoming.argument_count = 2U;
    for (i = 0U; i < 2U; ++i) {
        incoming.arguments[i].kind = MINIC_LP64_ARGUMENT_GP;
        incoming.arguments[i].gp_registers = 1U;
        incoming.arguments[i].gp_first = i;
        incoming.arguments[i].size_bytes = 8U;
    }
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = "loong_record_transfer"
    };
    stage = "select";
    if (!minic_loongarch64_select_machine(&machine, &ctx)) goto done;
    stage = "coverage";
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        goto done;
    stage = "policy";
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(&target,
            &policy)) goto done;
    stage = "preflight";
    if (!minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "gnu-emission";
    assembly = loong_test_tempfile();
    if (assembly == NULL ||
        !minic_machine_gnu_asm_emit_body(assembly, &machine, &policy) ||
        fflush(assembly) != 0 || fseek(assembly, 0L, SEEK_SET) != 0)
        goto done;
    text_length = fread(assembly_text, 1U, sizeof(assembly_text) - 1U,
        assembly);
    assembly_text[text_length] = '\0';
    if (strstr(assembly_text, "ld.bu") == NULL ||
        strstr(assembly_text, "st.b") == NULL ||
        (bytes >= 8U && strstr(assembly_text, "bnez") == NULL))
        goto done;
    stage = "effects";
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *block = &machine.blocks[i];
        if (block->machine_auxiliary) ++auxiliary;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *m = &block->instructions[j];
            if (!m->has_core_origin || m->core_instruction_id != 2U)
                continue;
            if ((m->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) != 0U) {
                ++effect_reads;
                if ((m->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U)
                    ++volatile_reads;
            }
            if ((m->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U) {
                ++effect_writes;
                if ((m->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U)
                    ++volatile_writes;
            }
        }
    }
    ok = effect_reads >= 1U && effect_writes >= 1U &&
        (bytes >= 8U ? auxiliary >= 3U : auxiliary == 1U) &&
        (mode != 2U ? volatile_reads >= 1U &&
                          volatile_writes == 0U :
            volatile_reads == 0U && volatile_writes == 0U);
done:
    if (!ok) fprintf(stderr,
        "LOONG_CORE_RECORD_TRANSFER=FAIL stage=%s mode=%u bytes=%zu\n",
        stage, mode, bytes);
    if (assembly != NULL) fclose(assembly);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* The LoongArch64 psABI distinguishes a <=16-byte register-class record
 * from a larger indirect record. Both must land in private Core object
 * storage before the first user instruction, with no textual byte loop. */
static bool test_record_parameter(bool indirect) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_ty, record_ty;
    MinicCoreInstruction ins = {0};
    MinicCoreBlockId entry;
    MinicCoreTerminator ret = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    size_t offset[1] = {32U};
    size_t writes = 0U, loops = 0U, i, j;
    const size_t bytes = indirect ? 32U : 13U;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_record_param"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_record_param", sizeof("loong_record_param"));
    core.name_length = sizeof("loong_record_param") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_ty)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &type, &record_ty) ||
        !minic_core_function_define_signature(&core, &types, void_ty,
            &record_ty, 1U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    core.objects = calloc(1U, sizeof(*core.objects));
    if (core.objects == NULL) goto done;
    core.objects[0].core_type_id = record_ty;
    core.object_count = core.object_capacity = 1U;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
    ins.core_type_id = void_ty;
    ins.value.parameter_object.parameter_index = 0U;
    ins.value.parameter_object.object_id = 0U;
    stage = "seal";
    if (!minic_core_function_append_effect_instruction(&core, entry, &ins))
        goto done;
    ret.kind = MINIC_CORE_TERMINATOR_RETURN;
    ret.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &ret) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 128U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    plan.object_count = 1U;
    plan.object_offsets = offset;
    incoming.argument_count = 1U;
    incoming.arguments[0].kind = indirect
        ? MINIC_LP64_ARGUMENT_INDIRECT_RECORD : MINIC_LP64_ARGUMENT_RECORD;
    incoming.arguments[0].size_bytes = bytes;
    incoming.arguments[0].gp_first = 0U;
    incoming.arguments[0].gp_registers = indirect ? 1U : 2U;
    incoming.arguments[0].chunks = indirect ? 0U : 2U;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = core.name
    };
    stage = "select";
    if (!minic_loongarch64_select_machine(&machine, &ctx)) goto done;
    stage = "coverage";
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        goto done;
    stage = "preflight";
    if (!minic_loongarch64_machine_gnu_asm_policy_initialize(&target,
            &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *block = &machine.blocks[i];
        if (block->machine_auxiliary && i != 0U) ++loops;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *m = &block->instructions[j];
            if (m->has_core_origin && m->core_instruction_id == 0U &&
                (m->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U)
                ++writes;
        }
    }
    ok = writes > 0U && (indirect ? loops >= 2U : loops == 0U);
done:
    if (!ok) fprintf(stderr,
        "LOONG_CORE_RECORD_PARAMETER=FAIL stage=%s indirect=%d\n",
        stage, (int)indirect);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Manufacture real Core storage shapes: the ABI test cannot pretend that a
 * 13-byte record with no fields is an integer-class aggregate. */
static bool test_record_type(MinicCoreTypeContext *types, size_t bytes,
    MinicCoreTypeId *record_id) {
    MinicCoreType type = {0};
    MinicCoreTypeId byte_id, array_id;
    MinicCoreRecordField field = {0};
    if (bytes == 0U) {
        type.kind = MINIC_CORE_TYPE_RECORD;
        type.has_layout = true;
        type.alignment = 1U;
        type.value.record.is_complete = true;
        return minic_core_type_register(types, &type, record_id);
    }
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 1U;
    type.value.integer.bit_width = 8U;
    if (!minic_core_type_register(types, &type, &byte_id)) return false;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_ARRAY;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.array.element = byte_id;
    type.value.array.count = bytes;
    type.value.array.is_complete = true;
    if (!minic_core_type_register(types, &type, &array_id)) return false;
    field.type_id = array_id;
    field.alignment = 1U;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    type.value.record.fields = &field;
    type.value.record.field_count = 1U;
    return minic_core_type_register(types, &type, record_id);
}

static const MinicDirectLP64AbiPolicy loong_call_test_abi = {
    .name = "loongarch64-lp64d",
    .gp_argument_registers = 8U,
    .fp_argument_registers = 8U,
    .stack_slot_bytes = 8U,
    .stack_alignment = 16U,
    .indirect_result_argument_registers = 1U,
    .core_indirect_record = minic_multi_indirect_record_core,
    .core_indirect_return = minic_multi_indirect_record_core,
    .record_registers = MINIC_DIRECT_LP64_RECORD_SPLIT_WITH_STACK,
    .wide_registers = MINIC_DIRECT_LP64_WIDE_SPLIT_WITH_STACK,
    .variadic_align_wide_gp_pair = true,
    .variadic_cursor = MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP,
    .variadic_save_bytes = 64U,
    .variadic_save_alignment = 8U,
    .variadic_requires_public_stack_alignment = true
};

/* mode=0: a7 + stack split a 13-byte record. mode=1: a hidden a0
 * return-buffer reservation pushes the eighth explicit, indirect-record
 * argument onto the stack. mode=2: zero-byte argument and result transport.
 * All plans come from the production Common
 * planner, never a hand-written per-target offset sketch. */
static bool test_record_call(unsigned int mode) {
    const bool hidden_return = mode == 1U;
    const bool empty = mode == 2U;
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_id, int_id, record_id, result_id, signature_id;
    MinicCoreTypeId parameter_ids[8];
    MinicCoreCallArgument args[8] = {{0}};
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId entry;
    MinicCoreValueId value;
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64CallPlanCache calls = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t value_offsets[7], object_offsets[2] = {160U, 192U};
    size_t begin, failed = SIZE_MAX, i;
    bool preflight, has_calls, ok = false;
    const char *stage = "setup";
    FILE *assembly = NULL;
    char text[12000];
    size_t read_count, call_effects = 0U;
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_record_call"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_record_call", sizeof("loong_record_call"));
    core.name_length = sizeof("loong_record_call") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_id)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 8U;
    type.value.integer.bit_width = 64U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &int_id) ||
        !test_record_type(&types, hidden_return ? 32U : empty ? 0U : 13U,
            &record_id) ||
        !minic_core_function_define_signature(&core, &types, void_id,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    result_id = (hidden_return || empty) ? record_id : void_id;
    for (i = 0U; i < 7U; ++i) parameter_ids[i] = int_id;
    parameter_ids[7] = record_id;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = result_id;
    type.value.function.parameters = parameter_ids;
    type.value.function.parameter_count = 8U;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &signature_id)) goto done;
    core.callees = calloc(1U, sizeof(*core.callees));
    core.objects = calloc(2U, sizeof(*core.objects));
    if (core.callees == NULL || core.objects == NULL) goto done;
    core.callee_count = core.callee_capacity = 1U;
    core.object_count = core.object_capacity = 2U;
    core.callees[0].name = malloc(sizeof("external_record_call"));
    if (core.callees[0].name == NULL) goto done;
    memcpy(core.callees[0].name, "external_record_call",
        sizeof("external_record_call"));
    core.callees[0].name_length = sizeof("external_record_call") - 1U;
    core.callees[0].core_function_type_id = signature_id;
    core.objects[0].core_type_id = record_id;
    core.objects[1].core_type_id = record_id;
    for (i = 0U; i < 7U; ++i) {
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        ins.core_type_id = int_id;
        ins.value.integer_value = (int64_t)(i + 1U);
        if (!minic_core_function_append_value_instruction(&core,
                entry, &ins, &value)) goto done;
        args[i].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        args[i].value.value_id = value;
        value_offsets[i] = i * 8U + 16U;
    }
    args[7].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
    args[7].value.object_id = 1U;
    stage = "call";
    if (!minic_core_function_append_call_arguments(&core, args, 8U, &begin))
        goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_CALL;
    ins.core_type_id = result_id;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.call.callee_id = 0U;
    ins.value.call.argument_begin = begin;
    ins.value.call.argument_count = 8U;
    ins.value.call.result_object = (hidden_return || empty) ? 0U :
        MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_effect_instruction(&core,
            entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    stage = "planner";
    if (!minic_lp64_plan_function_calls(&core, NULL,
            &loong_call_test_abi, "loongarch64", core.name,
            &failed, &preflight, &has_calls, &calls) ||
        !has_calls || calls.call_count != 1U ||
        calls.sites[0]->argument_count != 8U ||
        (empty ? calls.sites[0]->arguments[7].kind !=
            MINIC_LP64_ARGUMENT_EMPTY_RECORD :
            calls.sites[0]->arguments[7].stack_slots == 0U) ||
        (!hidden_return && !empty &&
            calls.sites[0]->arguments[7].gp_registers != 1U)) goto done;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 320U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame.has_calls = true;
    plan.value_count = 7U;
    plan.value_offsets = value_offsets;
    plan.object_count = 2U;
    plan.object_offsets = object_offsets;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .outgoing_calls = &calls, .name = core.name
    };
    stage = "select";
    if (!minic_loongarch64_select_machine(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "GNU";
    assembly = loong_test_tempfile();
    if (assembly == NULL ||
        !minic_machine_gnu_asm_emit_body(assembly, &machine, &policy) ||
        fflush(assembly) != 0 || fseek(assembly, 0L, SEEK_SET) != 0)
        goto done;
    read_count = fread(text, 1U, sizeof(text) - 1U, assembly);
    text[read_count] = '\0';
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *b = &machine.blocks[i];
        size_t j;
        for (j = 0U; j < b->instruction_count; ++j)
            if (b->instructions[j].has_core_origin &&
                b->instructions[j].core_instruction_id == 7U &&
                (b->instructions[j].effects &
                    MINIC_MACHINE_EFFECT_CALL) != 0U) ++call_effects;
    }
    stage = "semantics";
    ok = call_effects == 1U &&
        strstr(text, "  bl external_record_call") != NULL &&
        strstr(text, "st.d") != NULL &&
        (hidden_return ? strstr(text, "addi.d $a0") != NULL :
            empty ? strstr(text, "ld.bu") == NULL :
                strstr(text, "ld.bu") != NULL);
    if (!ok) goto done;
    /* Corrupt the prepared plan: the typed selector must reject an ABI
     * mismatch instead of emitting a partially plausible legacy fallback. */
    minic_machine_function_destroy(&machine);
    stage = "reject-bad-location";
    calls.sites[0]->arguments[7].kind = MINIC_LP64_ARGUMENT_GP;
    ok = !minic_loongarch64_select_machine(&machine, &ctx);
done:
    if (!ok) fprintf(stderr,
        "LOONG_CORE_RECORD_CALL=FAIL stage=%s mode=%u failed=%zu preflight=%d\n",
        stage, mode, failed, (int)preflight);
    if (assembly != NULL) fclose(assembly);
    minic_machine_function_destroy(&machine);
    minic_lp64_call_plan_cache_destroy(&calls);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Object-backed aggregate return uses the exact same Core storage layout
 * but the opposite ABI transport direction. Test both register and hidden
 * pointer paths, including an auxiliary byte-copy CFG for the latter. */
static bool test_record_return(unsigned int mode) {
    const bool indirect = mode == 1U;
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreTypeId record_id;
    MinicCoreBlockId entry;
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t object_offset = 32U, i, auxiliary = 0U;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_record_return"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_record_return", sizeof("loong_record_return"));
    core.name_length = sizeof("loong_record_return") - 1U;
    if (!test_record_type(&types,
            indirect ? 32U : mode == 2U ? 0U : 13U, &record_id) ||
        !minic_core_function_define_signature(&core, &types, record_id,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    core.objects = calloc(1U, sizeof(*core.objects));
    if (core.objects == NULL) goto done;
    core.objects[0].core_type_id = record_id;
    core.object_count = core.object_capacity = 1U;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_object = 0U;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 128U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    frame.has_indirect_return = indirect;
    frame.indirect_return_offset = 16U;
    plan.object_count = 1U;
    plan.object_offsets = &object_offset;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan, .name = core.name
    };
    stage = "select";
    if (!minic_loongarch64_select_machine(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i)
        if (machine.blocks[i].machine_auxiliary && i != 0U) ++auxiliary;
    ok = indirect ? auxiliary >= 2U : auxiliary == 0U;
    if (!ok) goto done;
    minic_machine_function_destroy(&machine);
    frame.has_indirect_return = !indirect;
    stage = "reject-bad-return-abi";
    ok = !minic_loongarch64_select_machine(&machine, &ctx);
done:
    if (!ok) fprintf(stderr,
        "LOONG_CORE_RECORD_RETURN=FAIL stage=%s mode=%u\n",
        stage, mode);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Parameterized Core overflow fixture: the same three input/value-pointer
 * locations exercise all previously supported Loong checked-arithmetic
 * classes without source C inference. Mode 0/1/2 are uniform signed ADD,
 * unsigned SUB and signed MUL; modes 3..6 are preexisting mixed forms. */
static bool test_integer_overflow(unsigned int mode) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicTargetInfo target = {0};
    MinicCoreType t = {0};
    MinicCoreTypeId void_ty, bool_ty, s32, u32, s64, u64, pointer_ty;
    MinicCoreTypeId formals[3], result_ty;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicCoreBlockId block;
    MinicCoreValueId ids[4];
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicLP64ArgumentPlan incoming = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const bool reachable[1] = {true};
    size_t value_offsets[4] = {16U, 24U, 32U, 40U};
    size_t i, j, writes = 0U;
    FILE *assembly = NULL;
    char assembly_text[8192];
    size_t text_length;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_checked_overflow"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_checked_overflow",
        sizeof("loong_checked_overflow"));
    core.name_length = sizeof("loong_checked_overflow") - 1U;
    t.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &t, &void_ty)) goto done;
    t = (MinicCoreType){0};
    t.kind = MINIC_CORE_TYPE_INTEGER;
    t.has_layout = true;
    t.size = t.alignment = 1U;
    t.value.integer.bit_width = 8U;
    t.value.integer.is_bool = true;
    if (!minic_core_type_register(&types, &t, &bool_ty)) goto done;
    t.size = t.alignment = 4U;
    t.value.integer.bit_width = 32U;
    t.value.integer.is_bool = false;
    t.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &t, &s32)) goto done;
    t.value.integer.is_signed = false;
    if (!minic_core_type_register(&types, &t, &u32)) goto done;
    t.size = t.alignment = 8U;
    t.value.integer.bit_width = 64U;
    t.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &t, &s64)) goto done;
    t.value.integer.is_signed = false;
    if (!minic_core_type_register(&types, &t, &u64)) goto done;
    formals[0] = mode == 8U ? s64 : mode == 9U ? u64 :
        mode == 0U || mode == 2U || mode == 3U || mode == 7U ? s32 :
        mode == 4U || mode == 5U ? u64 : u32;
    formals[1] = mode == 8U ? s64 : mode == 9U ? u64 :
        mode == 0U || mode == 2U ? s32 :
        mode == 1U || mode == 3U || mode == 4U || mode == 7U ? u32 :
        mode == 5U ? s32 : u64;
    result_ty = mode == 1U ? u32 : mode == 8U ? s64 :
        mode == 9U ? u64 : s32;
    t = (MinicCoreType){0};
    t.kind = MINIC_CORE_TYPE_POINTER;
    t.has_layout = true;
    t.size = t.alignment = 8U;
    t.value.pointer.pointee = result_ty;
    if (!minic_core_type_register(&types, &t, &pointer_ty)) goto done;
    formals[2] = pointer_ty;
    if (!minic_core_function_define_signature(&core, &types, bool_ty,
            formals, 3U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &block)) goto done;
    for (i = 0U; i < 3U; ++i) {
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
        ins.core_type_id = formals[i];
        ins.value.parameter_index = i;
        if (!minic_core_function_append_value_instruction(&core,
                block, &ins, &ids[i])) goto done;
    }
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW;
    ins.core_type_id = bool_ty;
    ins.value.integer_overflow.operator_kind = mode == 1U ?
        MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT :
        mode == 2U || mode == 7U || mode == 9U ?
            MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY :
        MINIC_CORE_INTEGER_OVERFLOW_ADD;
    ins.value.integer_overflow.left = ids[0];
    ins.value.integer_overflow.right = ids[1];
    ins.value.integer_overflow.result_address = ids[2];
    stage = "append-overflow";
    if (!minic_core_function_append_value_instruction(&core,
            block, &ins, &ids[3])) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = ids[3];
    if (!minic_core_function_set_terminator(&core, block, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_size = 128U;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    plan.value_count = 4U;
    plan.value_offsets = value_offsets;
    incoming.argument_count = 3U;
    for (i = 0U; i < 3U; ++i) {
        incoming.arguments[i].kind = MINIC_LP64_ARGUMENT_GP;
        incoming.arguments[i].gp_first = i;
        incoming.arguments[i].gp_registers = 1U;
        incoming.arguments[i].size_bytes = i == 2U ? 8U :
            formals[i] == u64 || formals[i] == s64 ? 8U : 4U;
    }
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan,
        .incoming_plan = &incoming, .name = core.name
    };
    stage = "machine";
    if (!minic_loongarch64_select_machine(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *b = &machine.blocks[i];
        for (j = 0U; j < b->instruction_count; ++j)
            if (b->instructions[j].has_core_origin &&
                b->instructions[j].core_instruction_id == 3U &&
                (b->instructions[j].effects &
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U) ++writes;
    }
    stage = "GNU-emission";
    assembly = loong_test_tempfile();
    if (assembly == NULL ||
        !minic_machine_gnu_asm_emit_body(assembly, &machine, &policy) ||
        fflush(assembly) != 0 || fseek(assembly, 0L, SEEK_SET) != 0)
        goto done;
    text_length = fread(assembly_text, 1U,
        sizeof(assembly_text) - 1U, assembly);
    assembly_text[text_length] = '\0';
    if (strstr(assembly_text, mode == 9U ? "mulh.du" :
            mode == 2U || mode == 7U ? "mul.d" :
            mode == 1U ? "sub.w" : mode == 0U ? "add.w" :
                "add.d") == NULL ||
        (strstr(assembly_text, "st.w") == NULL &&
            mode != 9U && mode != 8U))
        goto done;
    stage = "provenance";
    /* One store publishes the truncated result through Core's address,
     * another writes the boolean Core SSA spill into its frame slot. */
    ok = writes == 2U;
    if (!ok) goto done;
    minic_machine_function_destroy(&machine);
    stage = "poison";
    core.instructions[3].value.integer_overflow.operator_kind = 99;
    ok = !minic_loongarch64_select_machine(&machine, &ctx);
done:
    if (!ok) fprintf(stderr,
        "LOONG_CORE_OVERFLOW=FAIL stage=%s mode=%u\n", stage, mode);
    if (assembly != NULL) fclose(assembly);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* The compiler barrier is a Core memory-ordering effect, not an ISA fence.
 * Its exact Core origin survives as a zero-byte typed Machine instruction. */
static bool test_compiler_barrier(void) {
    MinicCoreTypeContext types;
    MinicCoreFunction core = {0};
    MinicCoreModule module = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_type;
    MinicCoreBlockId entry;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicTargetInfo target = {0};
    MinicMachineLP64FrameContract frame = {0};
    MinicLP64FramePlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    FILE *assembly = NULL;
    char text[2048];
    size_t i, j, barriers = 0U, bytes;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&core);
    core.name = malloc(sizeof("loong_core_compiler_barrier"));
    if (core.name == NULL) goto done;
    memcpy(core.name, "loong_core_compiler_barrier",
        sizeof("loong_core_compiler_barrier"));
    core.name_length = sizeof("loong_core_compiler_barrier") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_type) ||
        !minic_core_function_define_signature(&core, &types, void_type,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&core, &entry)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
    ins.core_type_id = void_type;
    if (!minic_core_function_append_effect_instruction(&core,
            entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_set_terminator(&core, entry, &term) ||
        !minic_core_function_attach_type_context(&core, &types)) goto done;
    target.architecture = MINIC_TARGET_ARCH_LOONGARCH64;
    target.data_layout = minic_loongarch64_linux_data_layout();
    module.type_context = &types;
    frame.frame_alignment = 16U;
    frame.saved_entry_sp_offset = SIZE_MAX;
    ctx = (MinicDirectLP64BlockContext){
        .fn = &core, .module = &module, .target = &target,
        .machine_frame = &frame, .frame_plan = &plan, .name = core.name
    };
    stage = "select";
    if (!minic_loongarch64_select_machine(&machine, &ctx) ||
        !minic_machine_core_coverage_verify(&core, reachable, &machine) ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            &target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i) {
        const MinicMachineBlock *b = &machine.blocks[i];
        for (j = 0U; j < b->instruction_count; ++j) {
            const MinicMachineInstruction *m = &b->instructions[j];
            if (m->kind != MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER)
                continue;
            if (!m->has_core_origin || m->core_instruction_id != 0U ||
                m->effects != MINIC_MACHINE_EFFECT_COMPILER_BARRIER)
                goto done;
            ++barriers;
        }
    }
    stage = "gnu-no-bytes";
    assembly = loong_test_tempfile();
    if (assembly == NULL ||
        !minic_machine_gnu_asm_emit_body(assembly, &machine, &policy) ||
        fflush(assembly) != 0 || fseek(assembly, 0L, SEEK_SET) != 0)
        goto done;
    bytes = fread(text, 1U, sizeof(text) - 1U, assembly);
    text[bytes] = '\0';
    if (barriers != 1U ||
        strstr(text, "MINIC_SOURCE_INLINE_ASM") != NULL ||
        strstr(text, "dbar") != NULL || strstr(text, "nop") != NULL)
        goto done;
    minic_machine_function_destroy(&machine);
    stage = "reject-malformed";
    core.instructions[0].core_type_id = MINIC_CORE_TYPE_INVALID;
    ok = !minic_loongarch64_select_machine(&machine, &ctx);
done:
    if (!ok) fprintf(stderr,
        "LOONG_CORE_COMPILER_BARRIER=FAIL stage=%s\n", stage);
    if (assembly != NULL) fclose(assembly);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&core);
    minic_core_type_context_destroy(&types);
    return ok;
}

int main(void) {
    if (!test_nonzero_core_entry() ||
        !test_record_transfer(4U, 0U) ||
        !test_record_transfer(32U, 0U) ||
        !test_record_transfer(32U, 1U) ||
        !test_record_transfer(16U, 2U) ||
        !test_record_parameter(false) ||
        !test_record_parameter(true) ||
        !test_record_call(0U) ||
        !test_record_call(1U) ||
        !test_record_call(2U) ||
        !test_record_return(0U) ||
        !test_record_return(1U) ||
        !test_record_return(2U) ||
        !test_integer_overflow(0U) ||
        !test_integer_overflow(1U) ||
        !test_integer_overflow(2U) ||
        !test_integer_overflow(3U) ||
        !test_integer_overflow(4U) ||
        !test_integer_overflow(5U) ||
        !test_integer_overflow(6U) ||
        !test_integer_overflow(7U) ||
        !test_integer_overflow(8U) ||
        !test_integer_overflow(9U) ||
        !test_compiler_barrier()) return 1;
    puts("LOONG_CORE_MIR_NONZERO_ENTRY=PASS prologue_targets_declared_entry=1 record_copy=1 record_load=1 vector_storage=1 volatile=1 byte_loop=1 record_param=2 record_call=3 record_return=3 overflow=10 compiler_barrier=1 bad_abi_reject=17");
    return 0;
}
