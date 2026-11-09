#include "target/x86_64/machine_frame_select.h"
#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/machine_target_state.h"
#include "target/x86_64/codegen_options.h"
#include "target/target_info.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prologue, out-of-line variadic spill and epilogue are genuine physical MIR.
 * In particular Core's first block has its own label and can be a backedge
 * without accidentally re-entering the ABI prologue. */

static void begin(MinicMachineFunction *function, MinicTargetInfo *target,
    const char *name, size_t frame_bytes,
    MinicFunctionCallingConvention convention) {
    memset(target, 0, sizeof(*target));
    target->architecture = MINIC_TARGET_ARCH_X86_64;
    target->data_layout = minic_x86_64_linux_data_layout();
    target->call_lowering_capabilities = MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR;
    assert(minic_machine_function_initialize(function, target, name,
        minic_x86_64_machine_generated_features(target)));
    function->calling_convention = convention;
    function->frame_size = frame_bytes;
    function->frame_alignment = 16U;
}

static void emit_jump(MinicMachineBuilder *builder,
    uint32_t opcode, size_t dest) {
    MinicMachineOpcodeContract contract = {0};
    MinicMachineOperand operand = {0};
    assert(minic_x86_64_machine_opcode_lookup(builder->function->target,
        opcode, &contract, NULL));
    assert(contract.operand_count == 1U);
    operand.kind = MINIC_MACHINE_OPERAND_BLOCK;
    operand.value.block_id = dest;
    assert(minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects, &operand, 1U,
        contract.implicit_count ? contract.implicit : NULL,
        contract.implicit_count));
    assert(minic_machine_builder_add_successor(builder, dest));
}

static char *serialize(MinicMachineFunction *function) {
    MinicMachineGNUAsmPolicy policy = {0};
    FILE *out;
    long size;
    char *buffer;
    assert(minic_x86_64_machine_gnu_asm_policy(function->target, &policy));
    assert(minic_machine_function_verify(function, &policy.verify));
    out = tmpfile();
    assert(out != NULL);
    assert(minic_machine_gnu_asm_emit_body(out, function, &policy));
    assert(fflush(out) == 0);
    size = ftell(out);
    assert(size > 0 && size < 65536L);
    assert(fseek(out, 0L, SEEK_SET) == 0);
    buffer = malloc((size_t)size + 1U);
    assert(buffer != NULL);
    assert(fread(buffer, 1U, (size_t)size, out) == (size_t)size);
    buffer[size] = '\0';
    assert(fclose(out) == 0);
    return buffer;
}

static void scenario_backedge(void) {
    MinicMachineFunction fn = {0};
    MinicTargetInfo target;
    MinicMachineBuilder builder;
    MinicX86MachinePrologue prologue;
    MinicX86MachineFrameView frame = {
        .frame_size = 64U, .calling_convention = MINIC_CALLING_CONVENTION_DEFAULT,
        .has_sret = true, .sret_save_offset = 8U,
        .saves_incoming_gp = true, .incoming_gp_save_offset = 16U};
    size_t core_entry, core_next, epilogue;
    char *s;
    begin(&fn, &target, "backedge", 64U,
        MINIC_CALLING_CONVENTION_DEFAULT);
    assert(minic_machine_builder_initialize(&builder, &fn));
    assert(minic_x86_64_machine_select_prologue(&builder, &frame, &prologue));
    assert(!prologue.has_variadic_fp_split);
    assert(prologue.entry_block == 0U);
    assert(minic_machine_builder_create_block(&builder, NULL, false, false,
        &core_entry));
    assert(minic_machine_builder_create_block(&builder, NULL, false, false,
        &core_next));
    assert(minic_machine_builder_create_block(&builder, NULL, false, true,
        &epilogue));
    assert(core_entry == 1U && core_next == 2U && epilogue == 3U);
    assert(minic_machine_builder_position_at(&builder,
        prologue.continuation_block));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, core_entry);
    assert(minic_machine_builder_position_at(&builder, core_entry));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, core_next);
    assert(minic_machine_builder_position_at(&builder, core_next));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, core_entry);
    assert(minic_machine_builder_position_at(&builder, epilogue));
    assert(minic_x86_64_machine_select_epilogue(&builder, &frame));
    s = serialize(&fn);
    assert(strstr(s, "  pushq %rbp\n  movq %rsp, %rbp\n") != NULL);
    assert(strstr(s, "  movq %rdi, -8(%rbp)\n") != NULL);
    assert(strstr(s, "  movq %r9, -56(%rbp)\n") != NULL);
    assert(strstr(s, "jmp .Lminic_mir_backedge_1") != NULL);
    assert(strstr(s, "jmp backedge\n") == NULL);
    assert(strstr(s, "  leave\n  ret\n") != NULL);
    free(s);
    minic_machine_function_destroy(&fn);
}

static void scenario_varargs(bool no_sse) {
    MinicMachineFunction fn = {0};
    MinicTargetInfo target;
    MinicMachineBuilder builder;
    MinicX86MachinePrologue prologue;
    MinicX86MachineFrameView frame = {
        .frame_size = 256U,
        .calling_convention = MINIC_CALLING_CONVENTION_DEFAULT,
        .has_varargs = true, .varargs_regsave_offset = 192U};
    size_t core, epilogue;
    char *s;
    begin(&fn, &target, no_sse ? "varargs_nosse" : "varargs_sse", 256U,
        MINIC_CALLING_CONVENTION_DEFAULT);
    if (no_sse) {
        assert(minic_x86_64_target_codegen_options_set(&target,
            MINIC_X86_CODEGEN_NO_SSE));
        fn.generated_features = minic_x86_64_machine_generated_features(&target);
    }
    assert(minic_machine_builder_initialize(&builder, &fn));
    assert(minic_x86_64_machine_select_prologue(&builder, &frame, &prologue));
    assert(prologue.has_variadic_fp_split == !no_sse);
    assert(minic_machine_builder_create_block(&builder, NULL, false, false,
        &core));
    assert(minic_machine_builder_create_block(&builder, NULL, false, true,
        &epilogue));
    assert(minic_machine_builder_position_at(&builder,
        prologue.continuation_block));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, core);
    assert(minic_machine_builder_position_at(&builder, core));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, epilogue);
    assert(minic_machine_builder_position_at(&builder, epilogue));
    assert(minic_x86_64_machine_select_epilogue(&builder, &frame));
    s = serialize(&fn);
    assert(strstr(s, "leaq -192(%rbp), %r10") != NULL);
    assert(strstr(s, "movq %r9, 40(%r10)") != NULL);
    if (no_sse) {
        assert(strstr(s, "movdqu") == NULL);
        assert(strstr(s, "testb %al, %al") == NULL);
    } else {
        assert(strstr(s, "testb %al, %al") != NULL);
        assert(strstr(s, "jz .Lminic_mir_varargs_sse_2") != NULL);
        assert(strstr(s, "movdqu %xmm7, 160(%r10)") != NULL);
    }
    free(s);
    minic_machine_function_destroy(&fn);
}

static void scenario_ms(void) {
    MinicMachineFunction fn = {0};
    MinicTargetInfo target;
    MinicMachineBuilder builder;
    MinicX86MachinePrologue prologue;
    MinicX86MachineFrameView frame = {
        .frame_size = 256U,
        .calling_convention = MINIC_CALLING_CONVENTION_MS_X64,
        .ms_gp_save_offset = 8U, .ms_xmm_save_offset = 32U};
    size_t core, epilogue;
    char *s;
    begin(&fn, &target, "ms", 256U, MINIC_CALLING_CONVENTION_MS_X64);
    assert(minic_machine_builder_initialize(&builder, &fn));
    assert(minic_x86_64_machine_select_prologue(&builder, &frame, &prologue));
    assert(minic_machine_builder_create_block(&builder, NULL, false, false,
        &core));
    assert(minic_machine_builder_create_block(&builder, NULL, false, true,
        &epilogue));
    assert(minic_machine_builder_position_at(&builder,
        prologue.continuation_block));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, core);
    assert(minic_machine_builder_position_at(&builder, core));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, epilogue);
    assert(minic_machine_builder_position_at(&builder, epilogue));
    assert(minic_x86_64_machine_select_epilogue(&builder, &frame));
    s = serialize(&fn);
    assert(strstr(s, "movq %rdi, -8(%rbp)") != NULL);
    assert(strstr(s, "movdqu %xmm6, -32(%rbp)") != NULL);
    assert(strstr(s, "movdqu -176(%rbp), %xmm15") != NULL);
    assert(strstr(s, "movq -16(%rbp), %rsi") != NULL);
    free(s);
    minic_machine_function_destroy(&fn);
}

static void scenario_ibt_realign_thunk(void) {
    MinicMachineFunction fn = {0};
    MinicTargetInfo target;
    MinicMachineBuilder builder;
    MinicX86MachinePrologue prologue;
    MinicX86MachineFrameView frame = {
        .frame_size = 128U,
        .calling_convention = MINIC_CALLING_CONVENTION_DEFAULT,
        .objects_from_aligned_rsp = true,
        .object_base_alignment = 64U};
    size_t core, epilogue;
    char *s;
    begin(&fn, &target, "ibt_realign", 128U,
        MINIC_CALLING_CONVENTION_DEFAULT);
    assert(minic_x86_64_target_codegen_options_set(&target,
        MINIC_X86_CODEGEN_IBT_ENDBR |
        MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK));
    fn.generated_features = minic_x86_64_machine_generated_features(&target);
    assert(minic_machine_builder_initialize(&builder, &fn));
    assert(minic_x86_64_machine_select_prologue(&builder, &frame, &prologue));
    assert(minic_machine_builder_create_block(&builder, NULL, false, false,
        &core));
    assert(minic_machine_builder_create_block(&builder, NULL, false, true,
        &epilogue));
    assert(minic_machine_builder_position_at(&builder,
        prologue.continuation_block));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, core);
    assert(minic_machine_builder_position_at(&builder, core));
    emit_jump(&builder, MINIC_X86_MI_JMP_B, epilogue);
    assert(minic_machine_builder_position_at(&builder, epilogue));
    assert(minic_x86_64_machine_select_epilogue(&builder, &frame));
    s = serialize(&fn);
    assert(strstr(s, "  endbr64\n  pushq %rbp\n") != NULL);
    assert(strstr(s, "  andq $-64, %rsp\n") != NULL);
    assert(strstr(s, "  leave\n  jmp __x86_return_thunk\n") != NULL);
    free(s);
    minic_machine_function_destroy(&fn);
}

static void scenario_reject_inconsistent_frame(void) {
    MinicMachineFunction fn = {0};
    MinicTargetInfo target;
    MinicMachineBuilder builder;
    MinicX86MachinePrologue out;
    MinicX86MachineFrameView bad = {
        .frame_size = 64U,
        .calling_convention = MINIC_CALLING_CONVENTION_DEFAULT,
        .has_varargs = true,
        .varargs_regsave_offset = 32U};
    begin(&fn, &target, "invalid_abi_frame", 64U,
        MINIC_CALLING_CONVENTION_DEFAULT);
    assert(minic_machine_builder_initialize(&builder, &fn));
    assert(!minic_x86_64_machine_select_prologue(&builder, &bad, &out));
    assert(fn.block_count == 0U && fn.frame_object_count == 0U);
    bad.has_varargs = false;
    bad.objects_from_aligned_rsp = true;
    bad.object_base_alignment = 24U;
    assert(!minic_x86_64_machine_select_prologue(&builder, &bad, &out));
    assert(fn.block_count == 0U);
    bad.objects_from_aligned_rsp = false;
    bad.has_sret = true;
    bad.sret_save_offset = 80U;
    assert(!minic_x86_64_machine_select_prologue(&builder, &bad, &out));
    assert(fn.block_count == 0U);
    minic_machine_function_destroy(&fn);
}

int main(void) {
    scenario_backedge();
    scenario_varargs(false);
    scenario_varargs(true);
    scenario_ms();
    scenario_ibt_realign_thunk();
    scenario_reject_inconsistent_frame();
    puts("x86 physical MachineIR ABI frames PASS");
    return 0;
}
