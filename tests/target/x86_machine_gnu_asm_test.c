#include "target/common/machine_gnu_asm.h"
#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/codegen_options.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This test is deliberately independent of the legacy Core-to-FILE* x86
 * emitter. It constructs selected physical operations, verifies their full
 * CFG/ABI and asks only the generic MachineFunction GNU serializer to print. */

static MinicMachineRegister gp(unsigned int number, unsigned int width) {
    MinicMachineRegister result = {0};
    result.bank = MINIC_MACHINE_REGISTER_GENERAL;
    result.number = number;
    result.width_bits = (uint16_t)width;
    return result;
}

static MinicMachineOperand reg(unsigned int number, unsigned int width,
    unsigned int access) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = gp(number,width);
    result.value.physical.access=access;
    return result;
}

static MinicMachineOperand imm(int64_t value) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_INTEGER;
    result.value.integer = value;
    return result;
}

static MinicMachineOperand symbol(const char *name, unsigned int relocation) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    result.value.symbol.name = (char *)name;
    result.value.symbol.relocation=relocation;
    return result;
}

static MinicMachineOperand block(size_t id) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_BLOCK;
    result.value.block_id=id;
    return result;
}

static MinicMachineOperand frame(size_t id) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_FRAME;
    result.value.frame.index=id;
    return result;
}

static MinicMachineOperand rip_got_address(const char *name) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.target_mode=0U;
    result.value.address.has_base=true;
    result.value.address.base=gp(MINIC_X86_REG_RIP,64U);
    result.value.address.scale=1U;
    result.value.address.has_symbol=true;
    result.value.address.symbol=(char *)name;
    result.value.address.relocation=MINIC_X86_MACHINE_RELOC_GOTPCREL;
    return result;
}

static MinicMachineOperand gs_address(void) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.has_base=true;
    result.value.address.base=gp(MINIC_X86_REG_R10,64U);
    result.value.address.has_segment=true;
    result.value.address.segment.bank=MINIC_MACHINE_REGISTER_SYSTEM;
    result.value.address.segment.number=MINIC_X86_SEG_GS;
    result.value.address.segment.width_bits=16U;
    result.value.address.scale=1U;
    return result;
}

static void append(MinicMachineFunction *function, size_t block_id,
    unsigned int opcode, const MinicMachineOperand *operands,
    size_t operand_count) {
    MinicMachineInstruction instruction = {0};
    MinicMachineOpcodeContract contract = {0};
    assert(minic_x86_64_machine_opcode_lookup(function->target,opcode,
        &contract,NULL));
    assert(operand_count == contract.operand_count);
    instruction.kind=MINIC_MACHINE_INSTRUCTION_SELECTED;
    instruction.opcode=opcode;
    instruction.effects=contract.required_effects;
    instruction.operands=(MinicMachineOperand *)operands;
    instruction.operand_count=operand_count;
    instruction.implicit=contract.implicit_count==0U ? NULL : contract.implicit;
    instruction.implicit_count=contract.implicit_count;
    assert(minic_machine_block_append_instruction(function,block_id,
        &instruction));
}

static void append0(MinicMachineFunction *function,size_t id,unsigned int opcode) {
    append(function,id,opcode,NULL,0U);
}

static void append1(MinicMachineFunction *function,size_t id,unsigned int opcode,
    MinicMachineOperand a) {
    append(function,id,opcode,&a,1U);
}

static void append2(MinicMachineFunction *function,size_t id,unsigned int opcode,
    MinicMachineOperand a,MinicMachineOperand b) {
    MinicMachineOperand args[2]={a,b};
    append(function,id,opcode,args,2U);
}

static bool rejected_without_output(MinicMachineFunction *function,
    const MinicMachineGNUAsmPolicy *policy) {
    FILE *stream=tmpfile();
    bool rejected;
    assert(stream != NULL);
    rejected=!minic_machine_gnu_asm_emit_body(stream,function,policy);
    assert(fflush(stream)==0);
    assert(ftell(stream)==0L);
    assert(fclose(stream)==0);
    return rejected;
}

static void build_function(MinicMachineFunction *function,
    const MinicTargetInfo *target) {
    MinicMachineFrameObject fp_slot={0},sp_slot={0};
    size_t entry=SIZE_MAX, cold=SIZE_MAX, taken=SIZE_MAX, done=SIZE_MAX;
    size_t fp=SIZE_MAX,sp=SIZE_MAX;
    assert(minic_machine_function_initialize(function,target,"mir_x86_probe",
        minic_x86_64_machine_generated_features(target)));
    function->frame_size=32U;
    function->frame_alignment=16U;
    fp_slot.size_bytes=8U;
    fp_slot.alignment_bytes=8U;
    fp_slot.area=MINIC_MACHINE_FRAME_LOCAL;
    fp_slot.base=MINIC_MACHINE_FRAME_BASE_FRAME_POINTER;
    fp_slot.has_fixed_offset=true;
    fp_slot.fixed_offset=-8;
    sp_slot=fp_slot;
    sp_slot.base=MINIC_MACHINE_FRAME_BASE_STACK_POINTER;
    sp_slot.fixed_offset=0;
    assert(minic_machine_function_add_frame_object(function,&fp_slot,&fp));
    assert(minic_machine_function_add_frame_object(function,&sp_slot,&sp));
    assert(minic_machine_function_add_block(function,NULL,false,&entry));
    assert(minic_machine_function_add_block(function,NULL,false,&cold));
    assert(minic_machine_function_add_block(function,NULL,false,&taken));
    assert(minic_machine_function_add_block(function,NULL,false,&done));
    assert(entry==0U && cold==1U && taken==2U && done==3U);

    append1(function,entry,MINIC_X86_MI_PUSH64_R,
        reg(MINIC_X86_REG_RBP,64U,MINIC_MACHINE_REGISTER_READ));
    append2(function,entry,MINIC_X86_MI_MOV64_RR,
        reg(MINIC_X86_REG_RSP,64U,MINIC_MACHINE_REGISTER_READ),
        reg(MINIC_X86_REG_RBP,64U,MINIC_MACHINE_REGISTER_WRITE));
    append2(function,entry,MINIC_X86_MI_SUB64_IR,imm(32),
        reg(MINIC_X86_REG_RSP,64U,
            MINIC_MACHINE_REGISTER_READ|MINIC_MACHINE_REGISTER_WRITE));
    append2(function,entry,MINIC_X86_MI_MOVABS64_IR,imm(INT64_MAX),
        reg(MINIC_X86_REG_RAX,64U,MINIC_MACHINE_REGISTER_WRITE));
    append2(function,entry,MINIC_X86_MI_MOV64_R_FRBP,
        reg(MINIC_X86_REG_RAX,64U,MINIC_MACHINE_REGISTER_READ),frame(fp));
    append2(function,entry,MINIC_X86_MI_MOV64_FRSP_R,frame(sp),
        reg(MINIC_X86_REG_R11,64U,MINIC_MACHINE_REGISTER_WRITE));
    append2(function,entry,MINIC_X86_MI_ADD64_RR,
        reg(MINIC_X86_REG_R11,64U,MINIC_MACHINE_REGISTER_READ),
        reg(MINIC_X86_REG_RAX,64U,
            MINIC_MACHINE_REGISTER_READ|MINIC_MACHINE_REGISTER_WRITE));
    append2(function,entry,MINIC_X86_MI_CMP64_IR,imm(7),
        reg(MINIC_X86_REG_RAX,64U,MINIC_MACHINE_REGISTER_READ));
    append1(function,entry,MINIC_X86_MI_JNZ_B,block(taken));
    assert(minic_machine_block_add_successor(function,entry,cold));
    assert(minic_machine_block_add_successor(function,entry,taken));
    assert(minic_machine_block_set_fallthrough(function,entry,cold));

    append2(function,cold,MINIC_X86_MI_LEA64_RIP_SYMBOL_R,
        symbol("mir_global",MINIC_X86_MACHINE_RELOC_PC32),
        reg(MINIC_X86_REG_R11,64U,MINIC_MACHINE_REGISTER_WRITE));
    append2(function,cold,MINIC_X86_MI_MOV64_GOTPCREL_R,
        symbol("mir_external",MINIC_X86_MACHINE_RELOC_GOTPCREL),
        reg(MINIC_X86_REG_R10,64U,MINIC_MACHINE_REGISTER_WRITE));
    append2(function,cold,MINIC_X86_MI_MOV64_AR,
        rip_got_address("mir_external"),
        reg(MINIC_X86_REG_RAX,64U,MINIC_MACHINE_REGISTER_WRITE));
    append1(function,cold,MINIC_X86_MI_CALL_SYSV_SYMBOL,
        symbol("puts",MINIC_X86_MACHINE_RELOC_PLT32));
    append1(function,cold,MINIC_X86_MI_JMP_B,block(done));
    assert(minic_machine_block_add_successor(function,cold,done));

    append2(function,taken,MINIC_X86_MI_MOV8_AR,gs_address(),
        reg(MINIC_X86_REG_RAX,8U,MINIC_MACHINE_REGISTER_WRITE));
    append1(function,taken,MINIC_X86_MI_JMP_B,block(done));
    assert(minic_machine_block_add_successor(function,taken,done));

    append0(function,done,MINIC_X86_MI_LEAVE);
    append0(function,done,MINIC_X86_MI_RET);
}

int main(void) {
    MinicTargetInfo machine_target={0};
    const MinicTargetInfo *target=&machine_target;
    MinicMachineGNUAsmPolicy policy={0};
    MinicMachineFunction function={0};
    MinicTargetInfo copy;
    FILE *stream;
    char result[16384];
    size_t bytes;
    int64_t saved;
    machine_target.architecture=MINIC_TARGET_ARCH_X86_64;
    machine_target.data_layout=minic_x86_64_linux_data_layout();
    machine_target.call_lowering_capabilities=
        MINIC_TARGET_CALL_MS_X64_FIXED_SCALAR;
    assert(target->data_layout != NULL);
    assert(minic_x86_64_machine_gnu_asm_policy(target,&policy));
    build_function(&function,target);
    assert(minic_machine_function_verify(&function,&policy.verify));

    stream=tmpfile();
    assert(stream != NULL);
    assert(minic_machine_gnu_asm_emit_body(stream,&function,&policy));
    assert(fflush(stream)==0);
    assert(fseek(stream,0L,SEEK_SET)==0);
    bytes=fread(result,1U,sizeof(result)-1U,stream);
    result[bytes]='\0';
    assert(fclose(stream)==0);
    assert(strstr(result,"movabsq $9223372036854775807, %rax")!=NULL);
    assert(strstr(result,"movq %rax, -8(%rbp)")!=NULL);
    assert(strstr(result,"leaq mir_global(%rip), %r11")!=NULL);
    assert(strstr(result,"movq mir_external@GOTPCREL(%rip), %r10")!=NULL);
    assert(strstr(result,"movq mir_external@GOTPCREL(%rip), %rax")!=NULL);
    assert(strstr(result,"movb %gs:(%r10), %al")!=NULL);
    assert(strstr(result,"call puts@PLT")!=NULL);
    assert(strstr(result,"jnz .Lminic_mir_mir_x86_probe_2")!=NULL);

    {
        const char *dump_path=getenv("MINIC_X86_MACHINE_ASM_DUMP_PATH");
        if (dump_path != NULL && dump_path[0] != '\0') {
            FILE *dump=fopen(dump_path,"w");
            assert(dump != NULL);
            assert(fputs(".text\n.globl mir_x86_probe\n"
                         "mir_x86_probe:\n",dump) != EOF);
            assert(fwrite(result,1U,bytes,dump)==bytes);
            assert(fclose(dump)==0);
        }
    }

    /* The complete verifier rejects invalid signed imm32 encodings before
     * the serializer writes its first byte. */
    saved=function.blocks[0].instructions[2].operands[0].value.integer;
    function.blocks[0].instructions[2].operands[0].value.integer=INT64_MAX;
    assert(rejected_without_output(&function,&policy));
    function.blocks[0].instructions[2].operands[0].value.integer=saved;

    saved=function.frame_objects[0].fixed_offset;
    function.frame_objects[0].fixed_offset=-INT64_C(0x80000000)-8;
    assert(rejected_without_output(&function,&policy));
    function.frame_objects[0].fixed_offset=saved;

    /* A forged PIC relocation embedded inside an x86 memory address must
     * not become a synthetic safe-looking RIP reference. */
    function.blocks[1].instructions[2].operands[0].value.address.displacement=8;
    assert(rejected_without_output(&function,&policy));
    function.blocks[1].instructions[2].operands[0].value.address.displacement=0;

    /* Frame-base identity is part of the machine contract, not deferred
     * until printing an incorrectly based load or store. */
    function.blocks[0].instructions[4].operands[1].value.frame.index=1U;
    assert(rejected_without_output(&function,&policy));
    function.blocks[0].instructions[4].operands[1].value.frame.index=0U;

    /* Arithmetic consumers cannot suppress hardware FLAGS definitions. */
    function.blocks[0].instructions[6].implicit[0].reg.width_bits=32U;
    assert(rejected_without_output(&function,&policy));
    function.blocks[0].instructions[6].implicit[0].reg.width_bits=64U;

    copy=*target;
    copy.call_lowering_capabilities=0U;
    function.target=&copy;
    function.calling_convention=MINIC_CALLING_CONVENTION_MS_X64;
    assert(rejected_without_output(&function,&policy));
    function.target=target;
    function.calling_convention=MINIC_CALLING_CONVENTION_DEFAULT;

    function.generated_features=0U;
    assert(rejected_without_output(&function,&policy));
    function.generated_features=minic_x86_64_machine_generated_features(target);
    assert(minic_machine_function_verify(&function,&policy.verify));
    minic_machine_function_destroy(&function);

    {
        MinicMachineFunction local={0};
        size_t entry;
        assert(minic_machine_function_initialize(&local,target,"mir_x86_ms",
            minic_x86_64_machine_generated_features(target)));
        local.calling_convention=MINIC_CALLING_CONVENTION_MS_X64;
        assert(minic_machine_function_add_block(&local,NULL,false,&entry));
        append1(&local,entry,MINIC_X86_MI_CALL_MS_LOCAL_SYMBOL,
            symbol("ms_helper",MINIC_X86_MACHINE_RELOC_PC32));
        append0(&local,entry,MINIC_X86_MI_RET);
        assert(minic_machine_function_verify(&local,&policy.verify));
        minic_machine_function_destroy(&local);
    }
    {
        MinicMachineFunction thunk={0};
        size_t entry;
        copy=*target;
        copy.x86_codegen_options=MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK;
        assert(minic_machine_function_initialize(&thunk,&copy,
            "mir_x86_retpoline",minic_x86_64_machine_generated_features(&copy)));
        assert(minic_machine_function_add_block(&thunk,NULL,false,&entry));
        append1(&thunk,entry,MINIC_X86_MI_JMP_INDIRECT_THUNK_RAX,
            symbol("__x86_indirect_thunk_rax",MINIC_X86_MACHINE_RELOC_PC32));
        assert(minic_machine_function_verify(&thunk,&policy.verify));
        /* Plain jmp *%rax must not bypass configured external retpoline. */
        thunk.blocks[0].instructions[0].opcode=MINIC_X86_MI_JMP_R;
        assert(rejected_without_output(&thunk,&policy));
        minic_machine_function_destroy(&thunk);
    }
    puts("X86_MACHINE_GNU_ASM=PASS typed_gpr_flags=PASS gotpcrel=PASS "
         "sysv_call=PASS verifier_negative=PASS");
    return 0;
}
