#define _POSIX_C_SOURCE 200809L
/* Genuine Core CFG→typed RV32I/RV64I selection: a backedge to the Core
 * entry must never reexecute the ABI prologue or address the function symbol. */
#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_target_state.h"
#include "target/riscv/instruction_select.h"
#include "target/common/machine_core_coverage.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "RV_CORE_MIR:%d: %s\n", __LINE__, #x); \
    return false; } } while (0)

/* Keep each test's temporary assembly stream inside the task workspace
 * when glibc's fixed /tmp mount is full; no unrelated /tmp entries touched. */
static FILE *rv_test_tmpfile(void) {
    const char *dir = getenv("TMPDIR");
    char path[1024];
    int n, fd;
    FILE *out;
    if (dir == NULL || dir[0] == '\0') return tmpfile();
    n = snprintf(path, sizeof(path), "%s/minic-rv-test-XXXXXX", dir);
    if (n < 0 || (size_t)n >= sizeof(path)) return NULL;
    fd = mkstemp(path);
    if (fd < 0) return NULL;
    (void)unlink(path);
    out = fdopen(fd, "w+");
    if (out == NULL) (void)close(fd);
    return out;
}

static bool test_fixed_tp_register(const char *name, size_t length) {
    return name != NULL && length == 2U && memcmp(name, "tp", 2U) == 0;
}

/* Exercise the SAME function-lifetime psABI plans consumed by proven old
 * RV32/RV64 emitters. A hand-written a0+i placement in a test would hide
 * exactly the migration regression this gate must detect. */
static bool prepare_old_rv_abi(const MinicCoreFunction *fn, unsigned xlen,
    size_t expected_calls, Rv32FunctionAbiPlan *abi32,
    MinicRiscv64FunctionAbiPlan *abi64, MinicCoreCallIndex *calls64,
    MinicRiscvCoreMachinePlan *bridge) {
    if (xlen == 32U) {
        if (!rv32_function_abi_plan_build(fn, NULL, abi32)) return false;
        bridge->abi32 = abi32;
        return true;
    }
    if (xlen != 64U ||
        !minic_riscv64_function_abi_plan_build_profile(fn, abi64, true) ||
        !minic_core_call_index_build(fn, expected_calls, calls64) ||
        !minic_riscv64_call_plan_cache_build(fn, true, calls64,
            &abi64->outgoing_calls)) return false;
    if (!minic_riscv64_function_abi_view_from_prepared(
            abi64, &bridge->abi64_view)) return false;
    bridge->abi64 = &bridge->abi64_view;
    bridge->semantic_calls64 = calls64;
    return true;
}

static bool core_cycle(MinicCoreFunction *fn, MinicCoreTypeContext *types,
                       unsigned xlen, bool compiler_barrier, bool source_asm,
                       bool fixed_register) {
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
    if (source_asm) {
        MinicCoreInstruction ins = {0};
        MinicCoreInlineAsmId source_id;
        CHECK(minic_core_function_add_opaque_inline_asm(fn,
            "addi x0, x0, 0", sizeof("addi x0, x0, 0") - 1U,
            true, true, &source_id));
        fn->inline_asms[source_id].source_asm_unique_id = 87U;
        ins.kind = MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM;
        ins.core_type_id = void_type;
        ins.result = MINIC_CORE_VALUE_INVALID;
        ins.value.inline_asm_id = source_id;
        CHECK(minic_core_function_append_effect_instruction(fn, entry, &ins));
        /* GNU asm volatile("") is an ordering event without hardware bytes;
         * it must neither disappear nor create an invented machine opcode. */
        CHECK(minic_core_function_add_opaque_inline_asm(fn,
            "", 0U, true, false, &source_id));
        fn->inline_asms[source_id].source_asm_unique_id = 88U;
        ins.value.inline_asm_id = source_id;
        CHECK(minic_core_function_append_effect_instruction(fn, entry, &ins));
    }
    if (fixed_register) {
        MinicCoreInstruction ins = {0};
        MinicCoreValueId result;
        MinicCoreTypeId word_type;
        MinicCoreFixedRegisterBinding *binding;
        type = (MinicCoreType){0};
        type.kind = MINIC_CORE_TYPE_INTEGER;
        type.has_layout = true;
        type.size = type.alignment = xlen / 8U;
        type.value.integer.bit_width = xlen;
        CHECK(minic_core_type_register(types, &type, &word_type));
        fn->fixed_register_bindings = calloc(1U, sizeof(*binding));
        CHECK(fn->fixed_register_bindings != NULL);
        fn->fixed_register_binding_count =
            fn->fixed_register_binding_capacity = 1U;
        binding = &fn->fixed_register_bindings[0];
        binding->register_name = malloc(3U);
        CHECK(binding->register_name != NULL);
        memcpy(binding->register_name, "tp", 3U);
        binding->register_name_length = 2U;
        binding->core_type_id = word_type;
        ins.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        ins.core_type_id = word_type;
        ins.value.fixed_register_binding_id = 0U;
        CHECK(minic_core_function_append_value_instruction(
            fn, entry, &ins, &result));
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE;
        ins.core_type_id = void_type;
        ins.result = MINIC_CORE_VALUE_INVALID;
        ins.value.fixed_register_write.binding_id = 0U;
        ins.value.fixed_register_write.source = result;
        CHECK(minic_core_function_append_effect_instruction(fn, entry, &ins));
    }
    term.kind = MINIC_CORE_TERMINATOR_BRANCH;
    term.branch_target = backedge;
    CHECK(minic_core_function_set_terminator(fn, entry, &term));
    term.branch_target = entry;
    CHECK(minic_core_function_set_terminator(fn, backedge, &term));
    CHECK(minic_core_function_attach_type_context(fn, types));
    return minic_core_function_verify(fn);
}

static bool run(unsigned xlen, bool compiler_barrier,
                bool exported_island, bool source_asm, bool fixed_register) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    MinicDataLayout layout = {0};
    const bool reachable[] = {true, true, true};
    const size_t fixed_value_offsets[] = {32U};
    FILE *out = NULL;
    char text[4096];
    size_t count;
    bool okay = false;
    const char *stage = "build-core";
    minic_core_type_context_initialize(&types);
    if (!core_cycle(&fn, &types, xlen, compiler_barrier, source_asm,
            fixed_register)) goto done;
    if (exported_island) {
        /* A disconnected exported local label is a live linker entry. The
         * legacy RV-private BFS dropped it; Core's shared closure keeps it. */
        MinicCoreBlockId island;
        MinicCoreTerminator ret = {0};
        if (!minic_core_function_add_block(&fn, &island) || island != 2U)
            goto done;
        fn.blocks[island].local_label_symbol = malloc(sizeof(".Luser_123"));
        if (fn.blocks[island].local_label_symbol == NULL) goto done;
        memcpy(fn.blocks[island].local_label_symbol, ".Luser_123",
            sizeof(".Luser_123"));
        ret.kind = MINIC_CORE_TERMINATOR_RETURN;
        ret.return_value = MINIC_CORE_VALUE_INVALID;
        ret.return_object = MINIC_CORE_OBJECT_INVALID;
        if (!minic_core_function_set_terminator(&fn, island, &ret) ||
            !minic_core_function_verify(&fn)) goto done;
    }
    stage = "build-target";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = xlen / 8U;
    layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    target.fixed_register_read = test_fixed_tp_register;
    target.fixed_register_write = test_fixed_tp_register;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    if (fixed_register) {
        plan.value_offsets = fixed_value_offsets;
        plan.value_offset_count = 1U;
    }
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan)) goto done;
    stage = "select";
    if (exported_island) {
        bool live[3] = {false, false, false};
        bool found_symbol = false;
        size_t block_index;
        /* The independent assembler-local symbol is a legitimate Core and
         * Machine root, with no fabricated edge from the ABI prologue.
         * Its absence would silently drop a linker-reachable label. */
        okay = minic_core_function_mark_reachable_blocks(
            &fn, live, 3U) && live[0] && live[1] && live[2] &&
            minic_riscv_machine_select_core_subset(&machine, &plan) &&
            machine.block_count == 8U &&
            minic_machine_core_coverage_verify(&fn, live, &machine);
        if (!okay) goto done;
        for (block_index = 0U; block_index < machine.block_count;
             ++block_index) {
            const MinicMachineBlock *block = &machine.blocks[block_index];
            if (block->has_core_block_origin && block->core_block_id == 2U)
                found_symbol = block_index != 0U &&
                    block->exported_label != NULL &&
                    strcmp(block->exported_label, ".Luser_123") == 0;
        }
        okay = found_symbol;
        goto done;
    }
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
    out = rv_test_tmpfile();
    stage = "assembly";
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
    if (source_asm)
        okay = okay && strstr(text, xlen == 64U ?
                "# MINIC_RV64I_ASM_BEGIN" :
                "# MINIC_SOURCE_INLINE_ASM_BEGIN") &&
            strstr(text, "  addi x0, x0, 0") &&
            strstr(text, xlen == 64U ?
                "# MINIC_RV64I_ASM_END" :
                "# MINIC_SOURCE_INLINE_ASM_END");
    if (fixed_register)
        okay = okay && strstr(text, "  mv x5, x4") &&
            strstr(text, "  mv x4, x5") &&
            strstr(text, xlen == 32U ? "  sw x5, 32(x2)" :
                                           "  sd x5, 32(x2)");
    if (!okay) fprintf(stderr, "RV%u asm:\n%s\n", xlen, text);
    if (okay && source_asm) {
        size_t block_index, instruction_index;
        bool observed = false, observed_empty = false;
        for (block_index = 0U; block_index < machine.block_count; ++block_index)
            for (instruction_index = 0U;
                 instruction_index < machine.blocks[block_index].instruction_count;
                 ++instruction_index) {
                const MinicMachineInstruction *m =
                    &machine.blocks[block_index].instructions[instruction_index];
                if (m->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
                    m->has_source_asm_unique_id &&
                    m->source_asm_unique_id == 87U &&
                    (m->effects & (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                                   MINIC_MACHINE_EFFECT_VOLATILE |
                                   MINIC_MACHINE_EFFECT_COMPILER_BARRIER)) ==
                    (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                     MINIC_MACHINE_EFFECT_VOLATILE |
                     MINIC_MACHINE_EFFECT_COMPILER_BARRIER))
                    observed = true;
                if (m->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM &&
                    m->has_source_asm_unique_id &&
                    m->source_asm_unique_id == 88U &&
                    m->source_asm_length == 0U &&
                    (m->effects & (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                                   MINIC_MACHINE_EFFECT_VOLATILE)) ==
                    (MINIC_MACHINE_EFFECT_SOURCE_ASM |
                     MINIC_MACHINE_EFFECT_VOLATILE) &&
                    (m->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) == 0U)
                    observed_empty = true;
            }
        okay = observed && observed_empty;
        /* No source identity and no unresolved physical clobber may enter
         * MachineIR. A failed selector must not leak a partial function. */
        minic_machine_function_destroy(&machine);
        fn.inline_asms[0].source_asm_unique_id = 0U;
        okay = okay && !minic_riscv_machine_select_core_subset(&machine, &plan);
        fn.inline_asms[0].source_asm_unique_id = 87U;
        okay = okay && minic_core_function_add_inline_asm_register_clobber(
            &fn, 0U, "t0", 2U) && minic_core_function_verify(&fn) &&
            !minic_riscv_machine_select_core_subset(&machine, &plan);
    }
    if (okay && fixed_register) {
        minic_machine_function_destroy(&machine);
        target.fixed_register_read = NULL;
        okay = !minic_riscv_machine_select_core_subset(&machine, &plan);
        target.fixed_register_read = test_fixed_tp_register;
        target.fixed_register_write = NULL;
        okay = okay && !minic_riscv_machine_select_core_subset(&machine, &plan);
    }
done:
    if (!okay) fprintf(stderr,
        "RV%u core-CFG exported=%d source_asm=%d fixed=%d barrier=%d at %s\n",
        xlen, exported_island, source_asm, fixed_register,
        compiler_barrier, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return okay;
}
static bool run_scalar_extended(unsigned xlen, MinicCoreInstructionKind binary,
    bool saved_return_address, unsigned bytes, bool is_signed,
    bool strict, bool native_m_available) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type, unsigned_type;
    MinicCoreTypeId scalar_type;
    MinicCoreBlockId entry;
    MinicCoreValueId values[4];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
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
    const bool bitscan = binary == MINIC_CORE_INSTRUCTION_INTEGER_CLZ ||
        binary == MINIC_CORE_INSTRUCTION_INTEGER_CTZ;
    const char *helper = minic_riscv_integer_arithmetic_symbol(
        binary, !is_signed, xlen);
    const char *stage = "initialize";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_scalar_core"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_scalar_core", sizeof("rv_scalar_core"));
    fn.name_length = sizeof("rv_scalar_core") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = bytes;
    type.value.integer.bit_width = bytes * 8U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &integer_type)) goto done;
    type.value.integer.is_signed = false;
    if (!minic_core_type_register(&types, &type, &unsigned_type)) goto done;
    scalar_type = is_signed ? integer_type : unsigned_type;
    if (!minic_core_function_define_signature(&fn, &types,
            bitscan ? integer_type : scalar_type,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "build instructions";
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.core_type_id = bitscan ? unsigned_type : scalar_type;
    ins.value.integer_value = 71;
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[0])) goto done;
    ins.value.integer_value = bitscan ? 27 : -27;
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[1])) goto done;
    ins.kind = binary;
    if (bitscan) {
        ins.value.operand = values[0];
        ins.core_type_id = integer_type;
    }
    else {
        ins.value.binary.left = values[0];
        ins.value.binary.right = values[1];
    }
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
    isa.strict_isa = strict;
    isa.codegen_features = native_m_available ? MINIC_RISCV_FEATURE_M : 0U;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 128U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = saved_return_address;
    frame.return_address_offset = 96U;
    for (i = 0U; i < 4U; ++i) offsets[i] = i * (xlen / 8U);
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = 4U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan)) goto done;
    stage = "select";
    if (helper != NULL && !saved_return_address) {
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
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
            &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    ok = strstr(assembly, "  li x5, 71") != NULL &&
        strstr(assembly, bitscan ? "  li x5, 27" : "  li x5, -27") != NULL &&
        (bitscan ? strstr(assembly, "  srl x5, x5, x28") != NULL &&
                   strstr(assembly, "  add x6, x6, x7") != NULL :
         helper != NULL ? strstr(assembly, helper) != NULL &&
                          strstr(assembly, "  call __") != NULL &&
                          strstr(assembly, "  mul x") == NULL &&
                          strstr(assembly, "  div x") == NULL &&
                          strstr(assembly, "  rem x") == NULL :
                          strstr(assembly, "  add x5, x5, x6") != NULL) &&
        strstr(assembly, "  neg x5, x5") != NULL &&
        strstr(assembly, "  ret") != NULL;
    if (!ok) fprintf(stderr, "RV%u scalar asm:\n%s\n", xlen, assembly);
done:
    if (!ok) fprintf(stderr,
        "RV%u scalar failed at %s op=%d width=%u signed=%d ra=%d strict=%d M=%d\n",
        xlen, stage, (int)binary, bytes, (int)is_signed,
        (int)saved_return_address, (int)strict, (int)native_m_available);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}

static bool run_scalar(unsigned xlen, MinicCoreInstructionKind binary,
                       bool saved_return_address) {
    return run_scalar_extended(xlen, binary, saved_return_address,
        4U, true, true, false);
}

/* The bcdd selector always chooses I-only libcalls, even with M available.
 * Exercise the ENTIRE narrow/full-XLEN integer family in both signednesses.
 * Unlike the earlier staged I-only subset, M is a positive base-I fallback. */
static bool run_integer_runtime_matrix(void) {
    const MinicCoreInstructionKind operations[] = {
        MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY,
        MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE,
        MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER
    };
    const unsigned xlens[] = {32U, 64U};
    size_t architecture, op;
    for (architecture = 0U; architecture < 2U; ++architecture)
        for (op = 0U; op < sizeof(operations) / sizeof(operations[0]); ++op)
            for (unsigned bytes = 1U; bytes <= xlens[architecture] / 8U;
                 bytes *= 2U)
                for (unsigned sign = 0U; sign < 2U; ++sign)
                    if (!run_scalar_extended(xlens[architecture], operations[op],
                            true, bytes, sign != 0U, true, false)) return false;
    return run_scalar_extended(32U, operations[0],
               false, 4U, true, true, false) &&
           run_scalar_extended(64U, operations[1],
               false, 8U, false, true, false) &&
           run_scalar_extended(32U, operations[2],
               true, 4U, false, true, true) &&
           run_scalar_extended(64U, operations[0],
               true, 8U, true, true, true) &&
           run_scalar_extended(64U, operations[1],
               true, 4U, true, false, false);
}


/* The same Core FP value must travel through typed MachineIR as raw
 * IEEE-754 bits on both ILP32 and LP64.  No F/D mnemonic may leak into the
 * generated stream even when the source target admits F/D assembly. */
static bool run_softfloat(unsigned xlen, unsigned bytes,
                          MinicCoreInstructionKind binary) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId fp_type;
    MinicCoreBlockId entry;
    MinicCoreValueId values[3];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t offsets[3], i, n;
    FILE *out = NULL;
    char assembly[8192];
    const char *helper = minic_riscv_softfloat_arithmetic_symbol(binary);
    bool ok = false;
    const char *stage = "initialize";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_softfloat_core"));
    if (fn.name == NULL || helper == NULL) goto done;
    memcpy(fn.name, "rv_softfloat_core", sizeof("rv_softfloat_core"));
    fn.name_length = sizeof("rv_softfloat_core") - 1U;
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = type.alignment = bytes;
    type.value.floating.format = bytes == 4U ?
        MINIC_CORE_FLOAT_BINARY32 : MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &fp_type) ||
        !minic_core_function_define_signature(&fn, &types, fp_type,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "build Core";
    ins.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
    ins.core_type_id = fp_type;
    ins.value.floating_bits = bytes == 4U ?
        UINT64_C(0x80000000) : UINT64_C(0x8000000000000000);
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[0])) goto done;
    ins.value.floating_bits = bytes == 4U ?
        UINT64_C(0x3f800000) : UINT64_C(0x3ff0000000000000);
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[1])) goto done;
    ins.kind = binary;
    ins.value.binary.left = values[0];
    ins.value.binary.right = values[1];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[2])) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = values[2];
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "target ABI";
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
    frame.return_address_offset = 96U;
    for (i = 0U; i < 3U; ++i) offsets[i] = 16U + i * 16U;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = 3U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan)) goto done;
    stage = "MachineIR";
    if (!minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "GNU asm";
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
            &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    ok = strstr(assembly, helper) != NULL &&
        strstr(assembly, "  call ") != NULL &&
        strstr(assembly, "  ret") != NULL &&
        strstr(assembly, "fadd.") == NULL &&
        strstr(assembly, "fsub.") == NULL &&
        strstr(assembly, "fmul.") == NULL &&
        strstr(assembly, "fdiv.") == NULL;
    if (!ok) fprintf(stderr,"RV%u softfloat %u asm:\n%s\n",
        xlen, bytes, assembly);
done:
    if (!ok) fprintf(stderr, "RV%u softfloat %u failed at %s\n",
        xlen, bytes, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Compare NaN-sensitive softfloat relations and the complete scalar
 * conversion/negation family through typed selected instructions on both
 * RV32I and RV64I. No direct FILE* emitter participates in this test. */
static bool run_float_runtime_family(unsigned xlen) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId void_t, int_t, float_t, double_t;
    MinicCoreBlockId entry;
    MinicCoreValueId values[12];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    size_t offsets[12], i, n;
    FILE *out = NULL;
    char assembly[16384];
    const char *required[] = {"__eqdf2", "__ledf2", "__extendsfdf2",
        "__truncdfsf2", "__floatsidf", "__fixdfsi"};
    bool ok = false;
    const char *stage = "initialize";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_float_runtime"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_float_runtime", sizeof("rv_float_runtime"));
    fn.name_length = sizeof("rv_float_runtime") - 1U;
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
    type.kind = MINIC_CORE_TYPE_FLOAT;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY32;
    if (!minic_core_type_register(&types, &type, &float_t)) goto done;
    type.size = type.alignment = 8U;
    type.value.floating.format = MINIC_CORE_FLOAT_BINARY64;
    if (!minic_core_type_register(&types, &type, &double_t) ||
        !minic_core_function_define_signature(&fn, &types, void_t,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "build core";
    ins.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
    ins.core_type_id = float_t;
    ins.value.floating_bits = UINT64_C(0x80000000);
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[0])) goto done;
    ins.core_type_id = double_t;
    ins.value.floating_bits = UINT64_C(0x3ff0000000000000);
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[1])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.core_type_id = int_t;
    ins.value.integer_value = -123;
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[2])) goto done;
    for (i = 0U; i < 3U; ++i) {
        ins.kind = i == 0U ? MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL :
            i == 1U ? MINIC_CORE_INSTRUCTION_DOUBLE_LESS :
                MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL;
        ins.core_type_id = int_t;
        ins.value.binary.left = values[1];
        ins.value.binary.right = values[1];
        if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
                &values[3U + i])) goto done;
    }
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE;
    ins.core_type_id = double_t;
    ins.value.operand = values[1];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[6])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
    ins.value.operand = values[0];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[7])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
    ins.core_type_id = float_t;
    ins.value.operand = values[1];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[8])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
    ins.core_type_id = double_t;
    ins.value.operand = values[2];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[9])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER;
    ins.core_type_id = int_t;
    ins.value.operand = values[1];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &values[10])) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "target";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 512U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = true;
    frame.return_address_offset = 448U;
    for (i = 0U; i < fn.value_count; ++i)
        offsets[i] = 16U + i * 16U;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = fn.value_count;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan)) goto done;
    stage = "select and preflight";
    if (!minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "serialize";
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
            &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    for (i = 0U; i < sizeof(required) / sizeof(required[0]); ++i)
        if (strstr(assembly, required[i]) == NULL) goto done;
    ok = strstr(assembly, "fadd.") == NULL &&
        strstr(assembly, "fmv.") == NULL &&
        strstr(assembly, "fcvt.") == NULL &&
        strstr(assembly, "  ret") != NULL;
done:
    if (!ok) fprintf(stderr, "RV%u FP runtime family failed at %s\n",
        xlen, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* Two allocations in one Core block verify the preplanned physical CFG
 * ordering: both unsigned-overflow branches trap and the successful moving
 * stack remains independent of the fixed s0-relative SSA frame. */
static bool run_dynamic_stack(unsigned xlen) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId count_t, ptr_t;
    MinicCoreBlockId entry;
    MinicCoreValueId value[3];
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const size_t offsets[3] = {16U, 32U, 48U};
    FILE *out = NULL;
    char assembly[8192];
    size_t i, n, traps = 0U;
    bool ok = false;
    const char *stage = "init";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_dynamic_stack"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_dynamic_stack", sizeof("rv_dynamic_stack"));
    fn.name_length = sizeof("rv_dynamic_stack") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.integer.bit_width = xlen;
    if (!minic_core_type_register(&types, &type, &count_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.pointer.pointee = count_t;
    if (!minic_core_type_register(&types, &type, &ptr_t) ||
        !minic_core_function_define_signature(&fn, &types, ptr_t,
            NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "Core dynamic";
    ins.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    ins.core_type_id = count_t;
    ins.value.integer_value = 41;
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &value[0])) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC;
    ins.core_type_id = ptr_t;
    ins.value.operand = value[0];
    if (!minic_core_function_append_value_instruction(&fn, entry, &ins,
            &value[1]) ||
        !minic_core_function_append_value_instruction(&fn, entry, &ins,
            &value[2])) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = value[2];
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "frame";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 256U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = true;
    frame.return_address_offset = 224U;
    frame.has_dynamic_stack = true;
    frame.saved_frame_base_offset = 216U;
    if (xlen == 32U) frame.outgoing_size = 32U;
    else frame.dynamic_call_area_size = 32U;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = 3U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan)) goto done;
    stage = "select";
    if (!minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    for (i = 0U; i < machine.block_count; ++i) {
        size_t j;
        for (j = 0U; j < machine.blocks[i].instruction_count; ++j) {
            const MinicMachineInstruction *m =
                &machine.blocks[i].instructions[j];
            if (m->opcode == MINIC_RISCV_MACHINE_EBREAK) ++traps;
        }
    }
    if (traps != 2U) goto done;
    stage = "serialize";
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
            &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    if (strstr(assembly, "  bltu x5, x6") == NULL ||
        strstr(assembly, "  bgeu x7, x5") == NULL ||
        strstr(assembly, "  sub x2, x2, x7") == NULL ||
        strstr(assembly, "  ebreak") == NULL) {
        fprintf(stderr, "RV%u dynamic asm:\n%s\n", xlen, assembly);
        goto done;
    }
    stage = "reject inconsistent frame";
    minic_machine_function_destroy(&machine);
    frame.has_dynamic_stack = false;
    if (minic_riscv_machine_select_core_subset(&machine, &plan)) goto done;
    ok = true;
done:
    if (!ok) fprintf(stderr, "RV%u dynamic stack failed at %s\n",
        xlen, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
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
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
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
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan)) goto done;
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
    out = rv_test_tmpfile();
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
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}
static bool run_indirect_call(unsigned xlen, bool stack_case) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId int_t, function_t, callback_t, args[10], params[9];
    MinicCoreValueId callee, argument, result;
    MinicCoreBlockId entry;
    MinicCoreInstruction ins = {0};
    MinicCoreCallArgument call_args[9] = {{0}};
    MinicCoreTerminator term = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineFunction machine = {0};
    MinicTargetInfo target = {0};
    MinicRiscvIsa isa = {0};
    MinicDataLayout layout = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t offsets[11], call_begin = SIZE_MAX, i, argument_count =
        stack_case ? 9U : 1U;
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
    for (i = 0U; i < argument_count; ++i) params[i] = int_t;
    type.value.function.parameters = params;
    type.value.function.parameter_count = argument_count;
    type.value.function.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    if (!minic_core_type_register(&types, &type, &function_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.pointer.pointee = function_t;
    if (!minic_core_type_register(&types, &type, &callback_t)) goto done;
    args[0] = callback_t;
    for (i = 0U; i < argument_count; ++i) args[i + 1U] = int_t;
    if (!minic_core_function_define_signature(&fn, &types, int_t,
            args, argument_count + 1U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    stage = "Core";
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = callback_t;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &callee)) goto done;
    for (i = 0U; i < argument_count; ++i) {
        ins.core_type_id = int_t;
        ins.value.parameter_index = i + 1U;
        if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &argument)) goto done;
        call_args[i].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        call_args[i].value.value_id = argument;
    }
    if (!minic_core_function_append_call_arguments(&fn,
        call_args, argument_count, &call_begin)) goto done;
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
    ins.value.indirect_call.argument_count = argument_count;
    ins.value.indirect_call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &result)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = result;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn) ||
        fn.value_count != argument_count + 2U) goto done;
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
    frame.frame_size = stack_case ? 256U : 128U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = true;
    frame.return_address_offset = frame.frame_size - 16U;
    frame.outgoing_size = stack_case ? 16U : 0U;
    for (i = 0U; i < argument_count + 2U; ++i)
        offsets[i] = (stack_case ? 32U : 0U) + i * (xlen / 8U);
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = offsets;
    plan.value_offset_count = argument_count + 2U;
    if (!prepare_old_rv_abi(&fn, xlen, 1U, &abi32, &abi64,
            &calls64, &plan)) goto done;
    if (!minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
        &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(text, 1U, sizeof(text) - 1U, out);
    text[n] = '\0';
    stage = "assembly";
    ok = strstr(text, "  jalr ra, 0(x5)") != NULL &&
         strstr(text, "  ret") != NULL &&
         (!stack_case ||
          (strstr(text, xlen == 32U ? "  sw x5, 0(x2)" :
              "  sd x5, 0(x2)") != NULL &&
           strstr(text, xlen == 32U ? "  lw x5, 260(x2)" :
              "  ld x5, 264(x2)") != NULL));
    if (ok) {
        /* A missing old final psABI plan MUST NOT silently fall back to
         * instruction-local a0+i placement, even for a simple CALL. */
        minic_machine_function_destroy(&machine);
        stage = "missing-final-abi-reject";
        if (xlen == 32U) plan.abi32 = NULL;
        else plan.abi64 = NULL;
        ok = !minic_riscv_machine_select_core_subset(&machine, &plan);
        plan.abi32 = xlen == 32U ? &abi32 : NULL;
        plan.abi64 = xlen == 64U ? &plan.abi64_view : NULL;
    }
    if (ok) {
        /* Neither incoming nor outgoing placements may be reconstructed
         * from Core parameter ordinal when the old psABI plan disagrees. */
        minic_machine_function_destroy(&machine);
        stage = "invalid-register-placement-reject";
        if (xlen == 32U)
            abi32.calls[0].arguments[0].location.register_begin = 8U;
        else
            abi64.outgoing_calls.sites[0]->arguments[0]
                .integer_register_begin = 8U;
        ok = !minic_riscv_machine_select_core_subset(&machine, &plan);
    }
    if (ok) {
        minic_machine_function_destroy(&machine);
        stage = "invalid-incoming-parameter-reject";
        if (xlen == 32U) {
            abi32.calls[0].arguments[0].location.register_begin = 0U;
            abi32.parameters[1].location.register_begin = 8U;
        } else {
            abi64.outgoing_calls.sites[0]->arguments[0]
                .integer_register_begin = 0U;
            abi64.parameter_locations[1].integer_register_begin = 8U;
        }
        ok = !minic_riscv_machine_select_core_subset(&machine, &plan);
    }
    if (ok && stack_case) {
        minic_machine_function_destroy(&machine);
        stage = "outgoing-stack-underreservation-reject";
        if (xlen == 32U)
            abi32.parameters[1].location.register_begin = 1U;
        else
            abi64.parameter_locations[1].integer_register_begin = 1U;
        frame.outgoing_size = 0U;
        ok = !minic_riscv_machine_select_core_subset(&machine, &plan);
    }
done:
    if (!ok) fprintf(stderr, "RV%u indirect-call stack=%d failure at %s\n", xlen, stack_case, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}

static bool run_record_parameter(unsigned xlen, size_t bytes,
    bool incoming_stack, bool relay_call) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId void_t, int_t, record_t, params[9];
    MinicCoreBlockId entry;
    MinicCoreInstruction ins = {0};
    MinicCoreCallArgument argument = {0};
    MinicCoreTerminator term = {0};
    MinicMachineFunction machine = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[1] = {true};
    size_t object_offsets[1] = {32U}, i, writes = 0U, reads = 0U;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_record_parameter"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_record_parameter", sizeof("rv_record_parameter"));
    fn.name_length = sizeof("rv_record_parameter") - 1U;
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
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = bytes;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &type, &record_t)) goto done;
    for (i = 0U; i < (incoming_stack ? 8U : 0U); ++i) params[i] = int_t;
    params[incoming_stack ? 8U : 0U] = record_t;
    if (!minic_core_function_define_signature(&fn, &types, void_t,
            params, incoming_stack ? 9U : 1U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    fn.objects = calloc(1U, sizeof(*fn.objects));
    if (fn.objects == NULL) goto done;
    fn.objects[0].core_type_id = record_t;
    fn.object_count = fn.object_capacity = 1U;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
    ins.core_type_id = void_t;
    ins.value.parameter_object.parameter_index = incoming_stack ? 8U : 0U;
    ins.value.parameter_object.object_id = 0U;
    if (!minic_core_function_append_effect_instruction(&fn,
            entry, &ins)) goto done;
    if (relay_call) {
        size_t argument_begin;
        MinicCoreType function_type = {0};
        MinicCoreTypeId sink_type;
        function_type.kind = MINIC_CORE_TYPE_FUNCTION;
        function_type.value.function.return_type = void_t;
        function_type.value.function.parameters = &record_t;
        function_type.value.function.parameter_count = 1U;
        function_type.value.function.calling_convention =
            MINIC_CALLING_CONVENTION_DEFAULT;
        if (!minic_core_type_register(&types, &function_type, &sink_type))
            goto done;
        fn.callees = calloc(1U, sizeof(*fn.callees));
        if (fn.callees == NULL) goto done;
        fn.callee_count = fn.callee_capacity = 1U;
        fn.callees[0].name = malloc(sizeof("rv_record_sink"));
        if (fn.callees[0].name == NULL) goto done;
        memcpy(fn.callees[0].name, "rv_record_sink",
            sizeof("rv_record_sink"));
        fn.callees[0].name_length = sizeof("rv_record_sink") - 1U;
        fn.callees[0].core_function_type_id = sink_type;
        argument.kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
        argument.value.object_id = 0U;
        if (!minic_core_function_append_call_arguments(&fn,
            &argument, 1U, &argument_begin)) goto done;
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_CALL;
        ins.core_type_id = void_t;
        ins.result = MINIC_CORE_VALUE_INVALID;
        ins.value.call.callee_id = 0U;
        ins.value.call.argument_begin = argument_begin;
        ins.value.call.argument_count = 1U;
        ins.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
        if (!minic_core_function_append_effect_instruction(&fn,
                entry, &ins)) goto done;
    }
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "select";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 256U;
    frame.stack_alignment = 16U;
    frame.saves_return_address = relay_call;
    frame.return_address_offset = 240U;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.object_offsets = object_offsets;
    plan.object_offset_count = 1U;
    if (!prepare_old_rv_abi(&fn, xlen, relay_call ? 1U : 0U,
            &abi32, &abi64,
            &calls64, &plan) ||
        !minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "effects";
    for (i = 0U; i < machine.block_count; ++i) {
        size_t j;
        const MinicMachineBlock *block = &machine.blocks[i];
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *m = &block->instructions[j];
            if (!m->has_core_origin || m->core_instruction_id != 0U) continue;
            if ((m->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U)
                ++writes;
            if ((m->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) != 0U)
                ++reads;
        }
    }
    ok = writes != 0U && (bytes <= 2U * (xlen / 8U) || reads != 0U);
done:
    if (!ok) fprintf(stderr,
        "RV%u record bytes=%zu stack=%d relay=%d failed stage=%s\n",
        xlen, bytes, incoming_stack, relay_call, stage);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}
static bool run_wide_parameter(unsigned xlen, bool split_register_stack) {
    MinicCoreFunction fn = {0};
    MinicCoreTypeContext types;
    MinicCoreType type = {0};
    MinicCoreTypeId int_t, wide_t, params[8];
    MinicCoreBlockId entry;
    MinicCoreValueId wide_value;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineFunction machine = {0};
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t value_offsets[] = {32U}, i;
    FILE *out = NULL;
    char assembly[8192];
    size_t n;
    bool ok = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_wide_parameter"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_wide_parameter", sizeof("rv_wide_parameter"));
    fn.name_length = sizeof("rv_wide_parameter") - 1U;
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    if (!minic_core_type_register(&types, &type, &int_t)) goto done;
    type.size = type.alignment = xlen == 32U ? 8U : 16U;
    type.value.integer.bit_width = 2U * xlen;
    if (!minic_core_type_register(&types, &type, &wide_t)) goto done;
    for (i = 0U; i < 7U; ++i) params[i] = int_t;
    params[split_register_stack ? 7U : 0U] = wide_t;
    if (!minic_core_function_define_signature(&fn, &types, wide_t,
            params, split_register_stack ? 8U : 1U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = wide_t;
    ins.value.parameter_index = split_register_stack ? 7U : 0U;
    if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &wide_value)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = wide_value;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "selector";
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
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = value_offsets;
    plan.value_offset_count = 1U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan) ||
        !minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "assembly";
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
        &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    ok = strstr(assembly, xlen == 32U ? "  sw x5, 32(x2)" :
        "  sd x5, 32(x2)") != NULL &&
        strstr(assembly, xlen == 32U ? "  sw x5, 36(x2)" :
        "  sd x5, 40(x2)") != NULL &&
        strstr(assembly, "  ret") != NULL;
    if (ok && split_register_stack)
        ok = strstr(assembly, xlen == 32U ? "  lw x5, 128(x2)" :
            "  ld x5, 128(x2)") != NULL;
done:
    if (!ok) fprintf(stderr,
        "RV%u wide split=%d failure at %s\n",
        xlen, split_register_stack, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return ok;
}

/* A 64-bit ILP32 Core condition is true when EITHER little-endian word is
 * nonzero. The low limb may be zero and the upper limb nonzero. LP64 has a
 * single 64-bit limb and must not materialize a phantom second chunk. */
static bool run_wide_condition(unsigned xlen, bool explicit_zero,
    bool constant, int64_t immediate) {
    MinicCoreTypeContext types;
    MinicCoreFunction fn = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_t, wide_t, int_t;
    MinicCoreBlockId entry, yes, no;
    MinicCoreValueId condition;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicRiscvCoreMachinePlan bridge = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicMachineFunction machine = {0};
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true, true, true};
    const size_t offsets[] = {32U, 64U};
    FILE *out = NULL;
    char assembly[8192];
    size_t n;
    bool okay = false;
    const char *stage = "Core";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_wide_condition"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_wide_condition", sizeof("rv_wide_condition"));
    fn.name_length = sizeof("rv_wide_condition") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&types, &type, &int_t)) goto done;
    type.value.integer.is_signed = false;
    type.size = type.alignment = (explicit_zero || constant) ?
        2U * (xlen / 8U) : 8U;
    type.value.integer.bit_width = (unsigned)(type.size * 8U);
    if (!minic_core_type_register(&types, &type, &wide_t) ||
        !minic_core_function_define_signature(&fn, &types,
            void_t, constant ? NULL : &wide_t, constant ? 0U : 1U,
            MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry) ||
        !minic_core_function_add_block(&fn, &yes) ||
        !minic_core_function_add_block(&fn, &no)) goto done;
    ins.kind = constant ? MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT :
        MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = wide_t;
    if (constant) ins.value.integer_value = immediate;
    else ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&fn,
        entry, &ins, &condition)) goto done;
    if (explicit_zero) {
        ins = (MinicCoreInstruction){0};
        ins.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        ins.core_type_id = int_t;
        ins.value.operand = condition;
        if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &condition)) goto done;
    }
    term.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    term.conditional.condition = condition;
    term.conditional.when_true = yes;
    term.conditional.when_false = no;
    if (!minic_core_function_set_terminator(&fn, entry, &term)) goto done;
    term = (MinicCoreTerminator){0};
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, yes, &term) ||
        !minic_core_function_set_terminator(&fn, no, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
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
    bridge.target = &target;
    bridge.core = &fn;
    bridge.frame = &frame;
    bridge.value_offsets = offsets;
    bridge.value_offset_count = explicit_zero ? 2U : 1U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &bridge) ||
        !minic_riscv_machine_select_core_subset(&machine, &bridge) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "assembly";
    out = rv_test_tmpfile();
    if (out == NULL || !minic_machine_gnu_asm_emit_body(out,
        &machine, &policy) || fflush(out) != 0 ||
        fseek(out, 0L, SEEK_SET) != 0) goto done;
    n = fread(assembly, 1U, sizeof(assembly) - 1U, out);
    assembly[n] = '\0';
    okay = strstr(assembly, "  beqz x5, ") != NULL &&
        (!constant ||
            (strstr(assembly, immediate < 0 ? "  li x5, -1" :
                 xlen == 32U ? "  li x5, 1" : "  li x5, 0") != NULL &&
             strstr(assembly, xlen == 32U ? "  sw x5, 36(x2)" :
                 "  sd x5, 40(x2)") != NULL)) &&
        (explicit_zero
            ? strstr(assembly, "  or x5, x5, x6") != NULL &&
              strstr(assembly, "  seqz x5, x5") != NULL &&
              strstr(assembly, xlen == 32U ? "  lw x6, 36(x2)" :
                  "  ld x6, 40(x2)") != NULL
            : xlen == 32U || constant
            ? strstr(assembly, xlen == 32U ? "  lw x5, 32(x2)" :
                  "  ld x5, 32(x2)") != NULL &&
              strstr(assembly, xlen == 32U ? "  lw x6, 36(x2)" :
                  "  ld x6, 40(x2)") != NULL &&
              strstr(assembly, "  or x5, x5, x6") != NULL
            : (strstr(assembly, "  ld x5, 32(x2)") != NULL ||
               strstr(assembly, "  mv x5, x10") != NULL) &&
              strstr(assembly, "  or x5, x5, x6") == NULL);
done:
    if (!okay) fprintf(stderr,
        "RV%u wide condition zero=%d constant=%d immediate=%" PRId64
        " failed at %s\n", xlen, explicit_zero, constant, immediate, stage);
    if (out != NULL) fclose(out);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return okay;
}

/* Packed records must remain byte-exact on both ISA widths, and each
 * independently volatile source/destination side survives Core→MIR. */
static bool run_record_memory(unsigned xlen) {
    MinicCoreTypeContext types;
    MinicCoreFunction fn = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_t, record_t, pointer_t, params[2];
    MinicCoreBlockId entry;
    MinicCoreValueId src, dst;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicRiscvIsa isa = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t value_offsets[2] = {32U, 48U};
    size_t object_offsets[] = {64U};
    size_t i, j, loads = 0U, stores = 0U, volatile_loads = 0U;
    size_t volatile_stores = 0U;
    bool okay = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_record_memory"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_record_memory", sizeof("rv_record_memory"));
    fn.name_length = sizeof("rv_record_memory") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.has_layout = true;
    type.size = 5U;
    type.alignment = 1U;
    type.value.record.is_complete = true;
    if (!minic_core_type_register(&types, &type, &record_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.pointer.pointee = record_t;
    if (!minic_core_type_register(&types, &type, &pointer_t)) goto done;
    params[0] = params[1] = pointer_t;
    if (!minic_core_function_define_signature(&fn, &types, void_t,
            params, 2U, MINIC_CALLING_CONVENTION_DEFAULT, false) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    fn.objects = calloc(1U, sizeof(*fn.objects));
    if (fn.objects == NULL) goto done;
    fn.objects[0].core_type_id = record_t;
    fn.object_count = fn.object_capacity = 1U;
    ins.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
    ins.core_type_id = pointer_t;
    ins.value.parameter_index = 0U;
    if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &src)) goto done;
    ins.value.parameter_index = 1U;
    if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &dst)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_LOAD;
    ins.core_type_id = record_t;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_load.source_address = src;
    ins.value.record_load.destination_object = 0U;
    ins.value.record_load.is_volatile = true;
    if (!minic_core_function_append_effect_instruction(&fn,
            entry, &ins)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    ins.core_type_id = record_t;
    ins.result = MINIC_CORE_VALUE_INVALID;
    ins.value.record_copy.destination_address = dst;
    ins.value.record_copy.source_address = src;
    ins.value.record_copy.destination_volatile = true;
    ins.value.record_copy.source_volatile = true;
    if (!minic_core_function_append_effect_instruction(&fn,
            entry, &ins)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = MINIC_CORE_VALUE_INVALID;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn)) goto done;
    stage = "machine";
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
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = value_offsets;
    plan.value_offset_count = 2U;
    plan.object_offsets = object_offsets;
    plan.object_offset_count = 1U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan) ||
        !minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "effects";
    for (i = 0U; i < machine.block_count; ++i)
        for (j = 0U; j < machine.blocks[i].instruction_count; ++j) {
            const MinicMachineInstruction *m =
                &machine.blocks[i].instructions[j];
            if (!m->has_core_origin ||
                (m->core_instruction_id != 2U &&
                 m->core_instruction_id != 3U)) continue;
            if ((m->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) != 0U) {
                ++loads;
                if ((m->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U)
                    ++volatile_loads;
            }
            if ((m->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U) {
                ++stores;
                if ((m->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U)
                    ++volatile_stores;
            }
        }
    okay = loads >= 10U && stores >= 10U &&
        volatile_loads >= 10U && volatile_stores >= 5U;
done:
    if (!okay) fprintf(stderr,
        "RV%u record memory failed at %s read=%zu write=%zu volatile=%zu/%zu\n",
        xlen, stage, loads, stores, volatile_loads, volatile_stores);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return okay;
}

/* Both LP64 and ILP32 consume saved return PC and the exact original-SP
 * variadic cursor. Level-one frame chains are not invented by the selector. */
static bool run_frame_varargs(unsigned xlen, bool realigned) {
    MinicCoreTypeContext types;
    MinicCoreFunction fn = {0};
    MinicCoreType type = {0};
    MinicCoreTypeId void_t, int_t, pointer_t;
    MinicCoreBlockId entry;
    MinicCoreValueId frame_value, return_value, va_value;
    MinicCoreInstruction ins = {0};
    MinicCoreTerminator term = {0};
    MinicTargetInfo target = {0};
    MinicDataLayout layout = {0};
    MinicRiscvIsa isa = {0};
    MinicRiscvPhysicalFrame frame = {0};
    MinicRiscvCoreMachinePlan plan = {0};
    Rv32FunctionAbiPlan abi32 = {0};
    MinicRiscv64FunctionAbiPlan abi64 = {0};
    MinicCoreCallIndex calls64 = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const bool reachable[] = {true};
    size_t value_offsets[] = {16U, 32U, 48U};
    size_t i, j, frame_loads = 0U, va_stores = 0U;
    bool okay = false;
    const char *stage = "setup";
    minic_core_type_context_initialize(&types);
    minic_core_function_initialize(&fn);
    fn.name = malloc(sizeof("rv_frame_varargs"));
    if (fn.name == NULL) goto done;
    memcpy(fn.name, "rv_frame_varargs", sizeof("rv_frame_varargs"));
    fn.name_length = sizeof("rv_frame_varargs") - 1U;
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&types, &type, &void_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    if (!minic_core_type_register(&types, &type, &int_t)) goto done;
    type = (MinicCoreType){0};
    type.kind = MINIC_CORE_TYPE_POINTER;
    type.has_layout = true;
    type.size = type.alignment = xlen / 8U;
    type.value.pointer.pointee = void_t;
    if (!minic_core_type_register(&types, &type, &pointer_t) ||
        !minic_core_function_define_signature(&fn, &types, pointer_t,
            &int_t, 1U, MINIC_CALLING_CONVENTION_DEFAULT, true) ||
        !minic_core_function_add_block(&fn, &entry)) goto done;
    ins.kind = MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS;
    ins.core_type_id = pointer_t;
    ins.value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_FRAME;
    if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &frame_value)) goto done;
    ins.value.call_frame_address.kind = MINIC_CORE_CALL_FRAME_ADDRESS_RETURN;
    if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &return_value)) goto done;
    ins = (MinicCoreInstruction){0};
    ins.kind = MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS;
    ins.core_type_id = pointer_t;
    if (!minic_core_function_append_value_instruction(&fn,
            entry, &ins, &va_value)) goto done;
    term.kind = MINIC_CORE_TERMINATOR_RETURN;
    term.return_value = va_value;
    term.return_object = MINIC_CORE_OBJECT_INVALID;
    if (!minic_core_function_set_terminator(&fn, entry, &term) ||
        !minic_core_function_attach_type_context(&fn, &types) ||
        !minic_core_function_verify(&fn) || frame_value != 0U ||
        return_value != 1U || va_value != 2U) goto done;
    stage = "machine";
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = layout.pointer_alignment = xlen / 8U;
    target.architecture = xlen == 32U ?
        MINIC_TARGET_ARCH_RISCV32 : MINIC_TARGET_ARCH_RISCV64;
    target.data_layout = &layout;
    isa.xlen = xlen;
    isa.abi = xlen == 32U ? MINIC_RISCV_ABI_ILP32 : MINIC_RISCV_ABI_LP64;
    isa.strict_isa = true;
    if (!minic_riscv_target_isa_store(&target, &isa)) goto done;
    frame.frame_size = 256U;
    frame.stack_alignment = realigned ? 64U : 16U;
    frame.has_dynamic_stack_alignment = realigned;
    frame.entry_sp_offset = 176U;
    frame.return_address_offset = 160U;
    frame.saves_return_address = true;
    frame.variadic = true;
    frame.first_variadic_gpr = 1U;
    frame.varargs_size = 7U * (xlen / 8U);
    frame.varargs_offset = frame.frame_size - frame.varargs_size;
    plan.target = &target;
    plan.core = &fn;
    plan.frame = &frame;
    plan.value_offsets = value_offsets;
    plan.value_offset_count = 3U;
    if (!prepare_old_rv_abi(&fn, xlen, 0U, &abi32, &abi64,
            &calls64, &plan) ||
        !minic_riscv_machine_select_core_subset(&machine, &plan) ||
        !minic_machine_core_coverage_verify(&fn, reachable, &machine) ||
        !minic_riscv_machine_gnu_asm_policy(&target, &policy) ||
        !minic_machine_gnu_asm_preflight(&machine, &policy)) goto done;
    stage = "effects";
    for (i = 0U; i < machine.block_count; ++i)
        for (j = 0U; j < machine.blocks[i].instruction_count; ++j) {
            const MinicMachineInstruction *m =
                &machine.blocks[i].instructions[j];
            if (m->has_core_origin && m->core_instruction_id == 1U &&
                (m->effects & MINIC_MACHINE_EFFECT_MEMORY_READ) != 0U)
                ++frame_loads;
            if (m->has_core_origin && m->core_instruction_id == 2U &&
                (m->effects & MINIC_MACHINE_EFFECT_MEMORY_WRITE) != 0U)
                ++va_stores;
        }
    if (frame_loads == 0U || va_stores == 0U) goto done;
    /* Core admits level-one frame introspection as a semantic expression,
     * but this backend deliberately has no physical stack-chain policy. */
    stage = "reject frame level one";
    minic_machine_function_destroy(&machine);
    fn.instructions[0].value.call_frame_address.level = 1U;
    if (!minic_core_function_verify(&fn) ||
        minic_riscv_machine_select_core_subset(&machine, &plan)) goto done;
    fn.instructions[0].value.call_frame_address.level = 0U;
    stage = "reject unavailable vararg state";
    frame.variadic = false;
    if (minic_riscv_machine_select_core_subset(&machine, &plan)) goto done;
    okay = true;
done:
    if (!okay) fprintf(stderr,
        "RV%u frame-varargs aligned=%d failed at %s\n",
        xlen, realigned, stage);
    minic_machine_function_destroy(&machine);
    rv32_function_abi_plan_destroy(&abi32);
    minic_riscv64_function_abi_plan_destroy(&abi64);
    minic_core_call_index_destroy(&calls64);
    minic_core_function_destroy(&fn);
    minic_core_type_context_destroy(&types);
    return okay;
}

int main(void) {
    if (!run(32U, false, false, false, false) ||
        !run(64U, false, false, false, false) ||
        !run(32U, true, false, false, false) ||
        !run(64U, true, false, false, false) ||
        !run(32U, false, true, false, false) ||
        !run(64U, false, true, false, false) ||
        !run(32U, false, false, true, false) ||
        !run(64U, false, false, true, false) ||
        !run(32U, false, false, false, true) ||
        !run(64U, false, false, false, true) ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_ADD, false) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_ADD, false) ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, false) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, false) ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, true) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, true) ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, true) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, true) ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER, true) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER, true) ||
        !run_integer_runtime_matrix() ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_CLZ, true) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_CLZ, true) ||
        !run_scalar(32U, MINIC_CORE_INSTRUCTION_INTEGER_CTZ, true) ||
        !run_scalar(64U, MINIC_CORE_INSTRUCTION_INTEGER_CTZ, true) ||
        !run_softfloat(32U, 4U, MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY) ||
        !run_softfloat(64U, 4U, MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY) ||
        !run_softfloat(32U, 8U, MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE) ||
        !run_softfloat(64U, 8U, MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE) ||
        !run_float_runtime_family(32U) ||
        !run_float_runtime_family(64U) ||
        !run_dynamic_stack(32U) ||
        !run_dynamic_stack(64U) ||
        !run_memory_family(32U) || !run_memory_family(64U) ||
        !run_indirect_call(32U, false) || !run_indirect_call(64U, false) ||
        !run_indirect_call(32U, true) || !run_indirect_call(64U, true) ||
        !run_record_parameter(32U, 5U, false, false) ||
        !run_record_parameter(64U, 5U, false, false) ||
        !run_record_parameter(32U, 17U, false, true) ||
        !run_record_parameter(64U, 17U, false, true) ||
        !run_record_parameter(32U, 17U, true, true) ||
        !run_record_parameter(64U, 17U, true, true) ||
        !run_wide_parameter(32U, false) ||
        !run_wide_parameter(64U, false) ||
        !run_wide_parameter(32U, true) ||
        !run_wide_parameter(64U, true) ||
        !run_wide_condition(32U, false, false, 0) ||
        !run_wide_condition(64U, false, false, 0) ||
        !run_wide_condition(32U, true, false, 0) ||
        !run_wide_condition(64U, true, false, 0) ||
        !run_wide_condition(32U, false, true, INT64_C(4294967296)) ||
        !run_wide_condition(32U, true, true, -1) ||
        !run_wide_condition(64U, false, true, INT64_C(4294967296)) ||
        !run_wide_condition(64U, true, true, -1) ||
        !run_record_memory(32U) || !run_record_memory(64U) ||
        !run_frame_varargs(32U, false) ||
        !run_frame_varargs(64U, false) ||
        !run_frame_varargs(32U, true) ||
        !run_frame_varargs(64U, true)) {
        fprintf(stderr, "RV_CORE_MIR_CFG=FAIL\n");
        return 1;
    }
    puts("RV_CORE_MIR_CFG=PASS architectures=2 prologue_isolated=1 exported_reachability=1 core_scalar=1 integer_libcall_matrix=42 policy_cases=5 memory_effects=1 record_memory=1 frame_varargs=1 indirect_call=1 stack_marshalling=1 wide_split=1 aggregate_parameter=1 aggregate_call=1 source_asm=1 fixed_register=1 abi_plan_reject=1 fail_closed=1");
    return 0;
}
