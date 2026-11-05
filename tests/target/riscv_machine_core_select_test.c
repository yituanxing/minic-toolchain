/* Genuine Core CFG→typed RV32I/RV64I selection: a backedge to the Core
 * entry must never reexecute the ABI prologue or address the function symbol. */
#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_target_state.h"
#include "target/common/machine_core_coverage.h"
#include "target/data_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "RV_CORE_MIR:%d: %s\n", __LINE__, #x); \
    return false; } } while (0)

static bool core_cycle(MinicCoreFunction *fn, MinicCoreTypeContext *types,
                       bool compiler_barrier) {
    MinicCoreType type = {0};
    MinicCoreTypeId void_type;
    MinicCoreBlockId entry, backedge;
    MinicCoreTerminator term = {0};
    minic_core_function_initialize(fn);
    fn->name = malloc(16U);
    CHECK(fn->name != NULL);
    memcpy(fn->name, "rv_typed_cycle", sizeof("rv_typed_cycle"));
    fn->name_length = sizeof("rv_typed_cycle") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    CHECK(minic_core_type_register(types, &type, &void_type));
    CHECK(minic_core_function_define_signature(fn, types, void_type, NULL,
        0U, MINIC_CALLING_CONVENTION_DEFAULT, false));
    CHECK(minic_core_function_add_block(fn, &entry) && entry == 0U);
    CHECK(minic_core_function_add_block(fn, &backedge) && backedge == 1U);
    if (compiler_barrier) {
        MinicCoreInstruction ins = {0};
        ins.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
        ins.core_type_id = void_type;
        ins.result = MINIC_CORE_VALUE_INVALID;
        CHECK(minic_core_function_append_effect_instruction(fn,
            entry, &ins));
    }
    term.kind = MINIC_CORE_TERMINATOR_BRANCH;
    term.branch_target = backedge;
    CHECK(minic_core_function_set_terminator(fn, entry, &term));
    term.branch_target = entry;
    CHECK(minic_core_function_set_terminator(fn, backedge, &term));
    CHECK(minic_core_function_attach_type_context(fn, types));
    return minic_core_function_verify(fn);
}

static bool run(unsigned xlen, bool compiler_barrier) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicDataLayout layout = {0};
    const bool reachable[] = {true, true};
    FILE *out = NULL;
    char text[4096];
    size_t count;
    bool okay = false;
    minic_core_type_context_initialize(&types);
    if (!core_cycle(&fn, &types, compiler_barrier)) goto done;
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = xlen / 8U;
    layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    if (!minic_riscv_machine_select_core_subset(&machine, &plan)) {
        fprintf(stderr, "RV%u: selector rejected basic CFG\n", xlen);
        goto done;
    }
    if (machine.block_count != 6U ||
        !machine.blocks[0].machine_auxiliary ||
        !machine.blocks[1].machine_auxiliary ||
        machine.blocks[2].machine_auxiliary ||
        !machine.blocks[2].has_core_block_origin ||
        machine.blocks[2].core_block_id != 0U ||
        !machine.blocks[3].machine_auxiliary ||
        machine.blocks[4].machine_auxiliary ||
        !machine.blocks[4].has_core_block_origin ||
        machine.blocks[4].core_block_id != 1U ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) {
        fprintf(stderr, "RV%u: machine CFG contract mismatch\n", xlen);
        goto done;
    }
    out = tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
        &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    count = fread(text, 1U, sizeof(text) - 1U, out);
    text[count] = '\0';
    okay = strstr(text, "rv_typed_cycle:") == NULL &&
        strstr(text, ".Lminic_mir_rv_typed_cycle_2:") != NULL &&
        strstr(text, ".Lminic_mir_rv_typed_cycle_4:") != NULL &&
        strstr(text, "  lla x31, .Lminic_mir_rv_typed_cycle_1") != NULL &&
        strstr(text, "  jalr zero, 0(x31)") != NULL &&
        strstr(text, "  ret") != NULL;
    if (!okay) fprintf(stderr, "RV%u asm:\n%s\n", xlen, text);
done:
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return okay;
}
static bool run_scalar(unsigned xlen, bool unsupported_multiply) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type;
    MinicCoreBlockId entry;
    MinicCoreValueId values[4];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t offsets[4];
    FILE *out = NULL;
    char assembly[4096];
    size_t i, n;
    bool ok = false;
    const char *stage = "initialize";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_scalar_core"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_scalar_core", sizeof("rv_scalar_core"));
    fn.name_length = sizeof("rv_scalar_core") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &integer_type) ||
        !minic_core_function_define_signature(&fn, &types, integer_type,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "build instructions";
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.core_type_id = integer_type;
    ins.value.integer_value = 71;
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[0])) goto done;
    ins.value.integer_value = -27;
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[1])) goto done;
    ins.kind = unsupported_multiply ?
        MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY :
        MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    ins.value.binary.left = values[0];
    ins.value.binary.right = values[1];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[2])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
    ins.value.operand = values[2];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[3])) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = values[3];
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "target initialize";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    for (i = 0U; i < 4U; ++i) offsets[i] = i * (xlen / 8U);
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = 4U;
    stage = "select";
    if (unsupported_multiply) {
        ok = !minic_riscv_machine_select_core_subset(&machine, &plan);
        goto done;
    }
    if (!minic_riscv_machine_select_core_subset(&machine, &plan)) {
        fprintf(stderr,"RV%u selection values=%zu objects=%zu instr=%zu\n",
            xlen,fn.value_count,fn.object_count,fn.instruction_count);
        goto done;
    }
    stage = "coverage";
    if (!minic_machine_core_coverage_verify(&fn, reachable, &machine)) goto done;
    stage = "preflight";
    if (!minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serialize";
    out = tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
            &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    ok = strstr(assembly, "  li x5, 71") != NULL &&
        strstr(assembly, "  li x5, -27") != NULL &&
        strstr(assembly, "  add x5, x5, x6") != NULL &&
        strstr(assembly, "  neg x5, x5") != NULL &&
        strstr(assembly, "  ret") != NULL;
    if (!ok) fprintf(stderr, "RV%u scalar asm:\n%s\n", xlen, assembly);
done:
    if (!ok) fprintf(stderr, "RV%u scalar Core selection failed at %s\n", xlen, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}
/* One Core family traverses the entire typed path for pointer scaling,
 * volatile reads/writes, relaxed atomic transfer and compiler barriers.
 * In strict RV32I/RV64I a nonpower-of-two stride must NEVER select M. */
static bool run_memory_family(unsigned xlen) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId void_t, int_t, ptr_t, args[2];
    MinicCoreBlockId entry;
    MinicCoreValueId ptr, index, scaled, negated, loaded, atomic_loaded;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineFunction machine = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    size_t offsets[6], i, j;
    bool saw_volatile_read = false, saw_volatile_write = false;
    bool saw_atomic_read = false, saw_atomic_write = false, saw_barrier = false;
    const bool reachable[] = {true};
    FILE *out = NULL;
    char text[8192];
    size_t n;
    bool ok = false;
    const char *stage = "init";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_core_memory"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_core_memory", sizeof("rv_core_memory"));
    fn.name_length = sizeof("rv_core_memory") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &int_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.pointer.pointee = int_t;
    if (!minic_core_type_register(&types, &type, &ptr_t)) goto done;
    args[0] = ptr_t;
    args[1] = int_t;
    if (!minic_core_function_define_signature(&fn, &types, ptr_t,
        args, 2U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "Core";
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = ptr_t;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &ptr)) goto done;
    ins.core_type_id = int_t;
    ins.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &index)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
    ins.core_type_id = ptr_t;
    ins.value.pointer_offset.base = ptr;
    ins.value.pointer_offset.index = index;
    ins.value.pointer_offset.element_size = 3U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &scaled)) goto done;
    ins.value.pointer_offset.element_size = 8U;
    ins.value.pointer_offset.subtract = true;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &negated)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_LOAD;
    ins.core_type_id = int_t;
    ins.value.load.address = ptr;
    ins.value.load.is_volatile = true;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &loaded)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_STORE;
    ins.core_type_id = void_t;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.store.address = negated;
    ins.value.store.stored_value = loaded;
    ins.value.store.is_volatile = true;
    if (!minic_core_function_append_effect_instruction(&fn,
        entry, &ins)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    ins.core_type_id = int_t;
    ins.value.atomic_load.address = ptr;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &atomic_loaded)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    ins.core_type_id = void_t;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.atomic_store.address = negated;
    ins.value.atomic_store.stored_value = atomic_loaded;
    if (!minic_core_function_append_effect_instruction(&fn,
        entry, &ins)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
    ins.core_type_id = void_t;
    ins.result = MINIC_CORE_VALUE_INVALID;
    if (!minic_core_function_append_effect_instruction(&fn,
        entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = scaled;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn) || fn.value_count != 6U) goto done;
    stage = "Machine";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    for (i = 0U; i < 6U; ++i) offsets[i] = i * (xlen / 8U);
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = 6U;
    if (!minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "effects";
    for (i = 0U; i < machine.block_count; ++i)
        for (j = 0U; j < machine.blocks[i].instruction_count; ++j) {
            const MinicMachineInstruction *op = &machine.blocks[i].instructions[j];
            if (!op->has_core_origin) continue;
            if (op->core_instruction_id == 4U &&
                (op->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                    MINIC_MACHINE_EFFECT_VOLATILE)) ==
                (MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_VOLATILE))
                saw_volatile_read = true;
            if (op->core_instruction_id == 5U &&
                (op->effects & (MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                    MINIC_MACHINE_EFFECT_VOLATILE)) ==
                (MINIC_MACHINE_EFFECT_MEMORY_WRITE | MINIC_MACHINE_EFFECT_VOLATILE))
                saw_volatile_write = true;
            if (op->core_instruction_id == 6U &&
                (op->effects & MINIC_MACHINE_EFFECT_MEMORY_READ))
                saw_atomic_read = true;
            if (op->core_instruction_id == 7U &&
                (op->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE))
                saw_atomic_write = true;
            if (op->core_instruction_id == 8U &&
                (op->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER))
                saw_barrier = true;
        }
    if (!saw_volatile_read || !saw_volatile_write || !saw_atomic_read ||
        !saw_atomic_write || !saw_barrier) goto done;
    stage = "assembly";
    out = tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
        &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(text, 1U, sizeof(text) - 1U, out);
    text[n] = '\0';
    ok = strstr(text, "  slli x28, x6, 1") != NULL &&
        strstr(text, "  add x7, x7, x28") != NULL &&
        strstr(text, "  slli x6, x6, 3") != NULL &&
        strstr(text, "  sub x5, x5, x6") != NULL &&
        strstr(text, "  ret") != NULL;
    /* An under-aligned Core atomic object can never silently become an
     * ordinary RV I load/store, even when its scalar width fits XLEN. */
    if (ok) {
        types.types[int_t].alignment = 1U;
        if (!minic_core_function_verify(&fn)) {
            ok = false;
            goto done;
        }
        minic_machine_function_destroy(&machine);
        machine = (MinicMachineFunction){0};
        stage = "atomic-alignment-reject";
        ok = !minic_riscv_machine_select_core_subset(&machine, &plan);
    }
done:
    if (!ok) fprintf(stderr, "RV%u memory family failure at %s\n", xlen, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}
static bool run_indirect_call(unsigned xlen) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId int_t, function_t, callback_t, args[2];
    MinicCoreValueId callee, argument, result;
    MinicCoreBlockId entry;
    MinicCoreInstruction ins = {0};
    MinicCoreCallArgument call_arg = {0};
    MinicCoreTerminator term = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineFunction machine = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t offsets[3], call_begin = SIZE_MAX, i;
    FILE *out = NULL;
    char text[4096];
    size_t n;
    bool ok = false;
    const char *stage = "init";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_core_indirect"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_core_indirect", sizeof("rv_core_indirect"));
    fn.name_length = sizeof("rv_core_indirect") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &int_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_FUNCTION;
    type.value.function.return_type = int_t;
    type.value.function.parameters = &int_t;
    type.value.function.parameter_count = 1U;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &function_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.pointer.pointee = function_t;
    if (!minic_core_type_register(&types, &type, &callback_t)) goto done;
    args[0] = callback_t;
    args[1] = int_t;
    if (!minic_core_function_define_signature(&fn, &types, int_t,
            args, 2U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "Core";
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = callback_t;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &callee)) goto done;
    ins.core_type_id = int_t;
    ins.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &argument)) goto done;
    call_arg.kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
    call_arg.value.value_id = argument;
    if (!minic_core_function_append_call_arguments(&fn,
        &call_arg, 1U, &call_begin)) goto done;
    fn.call_signatures = calloc(1U, sizeof(*fn.call_signatures));
    if (fn.call_signatures == NULL) goto done;
    fn.call_signature_count = fn.call_signature_capacity = 1U;
    fn.call_signatures[0].core_function_type_id = function_t;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    ins.core_type_id = int_t;
    ins.value.indirect_call.callee = callee;
    ins.value.indirect_call.signature_id = 0U;
    ins.value.indirect_call.argument_begin = call_begin;
    ins.value.indirect_call.argument_count = 1U;
    ins.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &result)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = result;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn) || fn.value_count != 3U) goto done;
    stage = "Machine";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = true;
    frame.return_address_offset = 112U;
    for (i = 0U; i < 3U; ++i) offsets[i] = i * (xlen / 8U);
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = 3U;
    if (!minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    out = tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
        &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(text, 1U, sizeof(text) - 1U, out);
    text[n] = '\0';
    stage = "assembly";
    ok = strstr(text, "  jalr ra, 0(x5)") != NULL &&
         strstr(text, "  ret") != NULL;
done:
    if (!ok) fprintf(stderr, "RV%u indirect-call failure at %s\n", xlen, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}
int main(void) {
    if (!run(32U, false) || !run(64U, false) ||
        !run(32U, true) || !run(64U, true) ||
        !run_scalar(32U, false) || !run_scalar(64U, false) ||
        !run_scalar(32U, true) || !run_scalar(64U, true) ||
        !run_memory_family(32U) || !run_memory_family(64U) ||
        !run_indirect_call(32U) || !run_indirect_call(64U)) {
        fprintf(stderr, "RV_CORE_MIR_CFG=FAIL\n");
        return 1;
    }
    puts("RV_CORE_MIR_CFG=PASS architectures=2 prologue_isolated=1 core_scalar=1 memory_effects=1 indirect_call=1 fail_closed=1");
    return 0;
}
