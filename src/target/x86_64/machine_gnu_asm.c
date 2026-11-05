#include "target/x86_64/machine_opcode.h"
#include "target/common/machine_gnu_asm.h"
#include "target/common/machine_source_asm_template.h"
#include "target/x86_64/asm_contract.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

static bool x86_emit_register(FILE *, const MinicMachineOperand *);
static bool x86_emit_address(FILE *, const MinicMachineAddress *);

static bool x86_source_accumulator_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        (instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_MULDIV_U64 ||
         instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_KASLR_MUL) &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction, NULL);
}

static bool x86_source_accumulator_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    size_t index, void *context) {
    return index < instruction->operand_count &&
        x86_source_accumulator_form(function, instruction, context);
}

static bool x86_source_accumulator_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    (void)labels;
    if (modifier != 0U ||
        !x86_source_accumulator_binding(function, instruction,
            index, context)) return false;
    return output == NULL ||
        x86_emit_register(output, &instruction->operands[index]);
}

static const MinicMachineSourceAsmTemplatePolicy x86_accumulator_policy = {
    x86_source_accumulator_form,
    x86_source_accumulator_binding,
    x86_source_accumulator_operand,
    NULL
};

/* Matched x86 source forms are distinct from the accumulator family: CPUID
 * uses four tied 32-bit named hardware registers, while MULACC reads a true
 * explicit memory operand in R11 and writes three tied 64-bit registers.
 * The target verifier independently proves every binding before this generic
 * GNU placeholder substitution may print a source spelling. */
static bool x86_source_matched_form(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction, void *context) {
    (void)context;
    return function != NULL && function->target != NULL &&
        (instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_CPUID_MATCHED ||
         instruction->source_asm_machine_form_id ==
             MINIC_X86_MACHINE_ASM_MULACC_MATCHED) &&
        minic_x86_64_machine_validate_source_asm(function->target,
            instruction, NULL);
}

static bool x86_source_matched_binding(
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    size_t index, void *context) {
    return index < instruction->operand_count &&
        x86_source_matched_form(function, instruction, context);
}

static bool x86_source_matched_operand(
    FILE *output, const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, size_t index,
    unsigned char modifier, void *context) {
    const MinicMachineOperand *operand;
    (void)labels;
    if (modifier != 0U ||
        !x86_source_matched_binding(function, instruction, index, context))
        return false;
    operand = &instruction->operands[index];
    if (output == NULL) return operand->kind == MINIC_MACHINE_OPERAND_REGISTER ||
        operand->kind == MINIC_MACHINE_OPERAND_ADDRESS;
    if (operand->kind == MINIC_MACHINE_OPERAND_REGISTER)
        return x86_emit_register(output, operand);
    return operand->kind == MINIC_MACHINE_OPERAND_ADDRESS &&
        x86_emit_address(output, &operand->value.address);
}

static const MinicMachineSourceAsmTemplatePolicy x86_matched_policy = {
    x86_source_matched_form,
    x86_source_matched_binding,
    x86_source_matched_operand,
    NULL
};

/* Only the seven Source-qualified, operand-free Linux barrier forms are
 * serialized here. Variant and source text are checked independently by the
 * target verifier before any function output is written. */
static bool x86_emit_source_asm(FILE *out,
    const MinicMachineFunction *function,
    const MinicMachineInstruction *instruction,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    static const char *const hardware[] = {
        "  lock; addl $0, -4(%rsp)\n", "  mfence\n", "  lfence\n",
        "  sfence\n", "  pause\n", "  rep; nop\n", "  fninit\n"
    };
    unsigned int variant;
    (void)labels;
    if (out == NULL || function == NULL ||
        !minic_x86_64_machine_validate_source_asm(function->target,
            instruction, context)) return false;
    if (instruction->source_asm_machine_form_id == 0U)
        return true; /* Empty volatile asm has an effect but zero bytes. */
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_PUT_USER_CALL ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_GET_USER_CALL)
        return true; /* Physical opcodes already emitted this private CALL. */
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MULDIV_U64)
        /* GNU's exact two-instruction form has a semicolon separator;
         * preserving a physical instruction on each line also preserves
         * the source-level ABI test's observable instruction boundaries. */
        return fputs("  mulq %r10\n  divq %r11\n", out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_KASLR_MUL)
        return fputs("  ", out) != EOF &&
            minic_machine_source_asm_template_emit(out, function,
                instruction, labels, &x86_accumulator_policy) &&
            fputc('\n', out) != EOF;
    if (instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_CPUID_MATCHED ||
        instruction->source_asm_machine_form_id ==
            MINIC_X86_MACHINE_ASM_MULACC_MATCHED)
        return fputs("  ", out) != EOF &&
            minic_machine_source_asm_template_emit(out, function,
                instruction, labels, &x86_matched_policy) &&
            fputc('\n', out) != EOF;
    variant = instruction->source_asm_machine_form_variant;
    return variant >= 1U && variant <=
            sizeof(hardware) / sizeof(hardware[0]) &&
        fputs(hardware[variant - 1U], out) != EOF;
}

/* The only x86 serializer for this physical integer-MIR subset. Every word
 * printed below derives from a recognized opcode plus independently verified
 * operands. There is no stored GNU syntax, FILE* trampoline or Core pointer. */
static const char *const x86_gp_names[4][16] = {
    {"%al","%cl","%dl","%bl","%spl","%bpl","%sil","%dil",
     "%r8b","%r9b","%r10b","%r11b","%r12b","%r13b","%r14b","%r15b"},
    {"%ax","%cx","%dx","%bx","%sp","%bp","%si","%di",
     "%r8w","%r9w","%r10w","%r11w","%r12w","%r13w","%r14w","%r15w"},
    {"%eax","%ecx","%edx","%ebx","%esp","%ebp","%esi","%edi",
     "%r8d","%r9d","%r10d","%r11d","%r12d","%r13d","%r14d","%r15d"},
    {"%rax","%rcx","%rdx","%rbx","%rsp","%rbp","%rsi","%rdi",
     "%r8","%r9","%r10","%r11","%r12","%r13","%r14","%r15"}
};

static const char *x86_gp_name(const MinicMachineRegister *reg) {
    unsigned int index;
    if (reg == NULL || reg->bank != MINIC_MACHINE_REGISTER_GENERAL)
        return NULL;
    if (reg->number == MINIC_X86_REG_RIP && reg->width_bits == 64U)
        return "%rip";
    if (reg->number >= MINIC_X86_REG_RIP) return NULL;
    switch (reg->width_bits) {
    case 8U: index = 0U; break;
    case 16U: index = 1U; break;
    case 32U: index = 2U; break;
    case 64U: index = 3U; break;
    default: return NULL;
    }
    return x86_gp_names[index][reg->number];
}

static bool x86_emit_gp_operand(FILE *out, const MinicMachineOperand *operand) {
    const char *name;
    if (out == NULL || operand == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_REGISTER ||
        (name = x86_gp_name(&operand->value.physical.reg)) == NULL)
        return false;
    return fputs(name,out) != EOF;
}

static bool x86_emit_register(FILE *out, const MinicMachineOperand *operand) {
    const MinicMachineRegister *reg;
    if (out == NULL || operand == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_REGISTER) return false;
    reg = &operand->value.physical.reg;
    if (reg->bank == MINIC_MACHINE_REGISTER_GENERAL)
        return x86_emit_gp_operand(out,operand);
    if (reg->bank == MINIC_MACHINE_REGISTER_VECTOR &&
        reg->number < 16U && reg->width_bits == 128U)
        return fprintf(out,"%%xmm%u",reg->number) >= 0;
    if (reg->bank == MINIC_MACHINE_REGISTER_FLOAT &&
        reg->number < 8U && reg->width_bits == 80U)
        return fprintf(out,"%%st(%u)",reg->number) >= 0;
    return false;
}

static bool x86_emit_symbol(FILE *out, const char *name, int64_t addend,
    unsigned int relocation) {
    const char *suffix;
    if (out == NULL || name == NULL || name[0] == '\0') return false;
    switch (relocation) {
    case MINIC_X86_MACHINE_RELOC_ABSOLUTE:
    case MINIC_X86_MACHINE_RELOC_ABS32S:
    case MINIC_X86_MACHINE_RELOC_PC32: suffix=""; break;
    case MINIC_X86_MACHINE_RELOC_GOTPCREL: suffix="@GOTPCREL"; break;
    case MINIC_X86_MACHINE_RELOC_PLT32: suffix="@PLT"; break;
    default: return false;
    }
    if (fputs(name,out) == EOF || fputs(suffix,out) == EOF) return false;
    if (addend > 0) return fprintf(out,"+%" PRId64,addend) >= 0;
    if (addend < 0) return fprintf(out,"%" PRId64,addend) >= 0;
    return true;
}

static bool x86_emit_address(FILE *out, const MinicMachineAddress *address) {
    const char *base,*index = NULL;
    if (out == NULL || address == NULL ||
        (base = x86_gp_name(&address->base)) == NULL) return false;
    if (address->has_index &&
        (index = x86_gp_name(&address->index)) == NULL) return false;
    if (address->has_segment) {
        if (address->segment.number == MINIC_X86_SEG_FS) {
            if (fputs("%fs:",out) == EOF) return false;
        } else if (address->segment.number == MINIC_X86_SEG_GS) {
            if (fputs("%gs:",out) == EOF) return false;
        } else return false;
    }
    if (address->has_symbol) {
        if (!x86_emit_symbol(out,address->symbol,address->addend,
                             address->relocation)) return false;
    } else if (address->displacement != 0 &&
               fprintf(out,"%" PRId64,address->displacement) < 0) return false;
    if (fprintf(out,"(%s",base) < 0) return false;
    if (index != NULL && fprintf(out,",%s,%u",index,address->scale) < 0)
        return false;
    return fputc(')',out) != EOF;
}

static bool x86_emit_frame(FILE *out, const MinicMachineFunction *function,
    const MinicMachineOperand *operand,
    const MinicX86MachineOpcodeInfo *opcode) {
    const MinicMachineFrameObject *obj;
    const char *base;
    int64_t displacement;
    if (out == NULL || function == NULL || operand == NULL || opcode == NULL ||
        operand->kind != MINIC_MACHINE_OPERAND_FRAME ||
        operand->value.frame.index >= function->frame_object_count)
        return false;
    obj = &function->frame_objects[operand->value.frame.index];
    if (opcode->frame_base == 1U &&
        obj->base != MINIC_MACHINE_FRAME_BASE_FRAME_POINTER) return false;
    if (opcode->frame_base == 2U &&
        obj->base != MINIC_MACHINE_FRAME_BASE_STACK_POINTER) return false;
    base = obj->base == MINIC_MACHINE_FRAME_BASE_FRAME_POINTER ? "%rbp" :
           obj->base == MINIC_MACHINE_FRAME_BASE_STACK_POINTER ? "%rsp" : NULL;
    if (base == NULL) return false;
    if ((operand->value.frame.displacement > 0 &&
         obj->fixed_offset > INT64_MAX - operand->value.frame.displacement) ||
        (operand->value.frame.displacement < 0 &&
         obj->fixed_offset < INT64_MIN - operand->value.frame.displacement))
        return false;
    displacement = obj->fixed_offset + operand->value.frame.displacement;
    if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
    return fprintf(out,"%" PRId64 "(%s)",displacement,base) >= 0;
}

static bool x86_emit_typed_operand(FILE *out,
    const MinicMachineFunction *function, const MinicMachineOperand *operand,
    const MinicX86MachineOpcodeInfo *opcode) {
    if (operand == NULL) return false;
    switch (operand->kind) {
    case MINIC_MACHINE_OPERAND_REGISTER:
        return x86_emit_register(out,operand);
    case MINIC_MACHINE_OPERAND_INTEGER:
        return fprintf(out,"$%" PRId64,operand->value.integer) >= 0;
    case MINIC_MACHINE_OPERAND_ADDRESS:
        return x86_emit_address(out,&operand->value.address);
    case MINIC_MACHINE_OPERAND_FRAME:
        return x86_emit_frame(out,function,operand,opcode);
    case MINIC_MACHINE_OPERAND_SYMBOL:
        return x86_emit_symbol(out,operand->value.symbol.name,
            operand->value.symbol.addend,operand->value.symbol.relocation);
    default: return false;
    }
}

static bool x86_emit_selected(FILE *out,
    const MinicMachineFunction *function, const MinicMachineInstruction *ins,
    const MinicMachineGNUAsmLabels *labels, void *context) {
    const MinicX86MachineOpcodeInfo *opcode;
    (void)context;
    if (out == NULL || function == NULL || ins == NULL || labels == NULL ||
        (opcode=minic_x86_64_machine_opcode_info(ins->opcode)) == NULL ||
        !minic_x86_64_machine_validate_selected(function,ins,NULL))
        return false;
    if (ins->opcode == MINIC_X86_MI_SHLD64_CL_RR ||
        ins->opcode == MINIC_X86_MI_SHRD64_CL_RR) {
        if (fprintf(out,"  %s %%cl, ",opcode->mnemonic) < 0 ||
            !x86_emit_gp_operand(out,&ins->operands[0]) ||
            fputs(", ",out) == EOF ||
            !x86_emit_gp_operand(out,&ins->operands[1])) return false;
        return fputc('\n',out) != EOF;
    }
    if (fprintf(out,"  %s",opcode->mnemonic) < 0) return false;
    switch (opcode->format) {
    case MINIC_X86_MACHINE_FORMAT_NONE:
        /* Fixed-register x87 stack encodings are target-known machine
         * opcodes, not source ASM templates or arbitrary GNU fragments. */
        switch (ins->opcode) {
        case MINIC_X86_MI_FXCH_ST1:
        case MINIC_X86_MI_FSTP_ST0:
            if (fputs(ins->opcode == MINIC_X86_MI_FXCH_ST1
                      ? " %st(1)" : " %st(0)",out) == EOF) return false;
            break;
        case MINIC_X86_MI_FUCOMIP_ST1:
            if (fputs(" %st(1), %st",out) == EOF) return false;
            break;
        case MINIC_X86_MI_FADDP_ST1:
        case MINIC_X86_MI_FSUBRP_ST1:
        case MINIC_X86_MI_FMULP_ST1:
        case MINIC_X86_MI_FDIVRP_ST1:
            if (fputs(" %st, %st(1)",out) == EOF) return false;
            break;
        default: break;
        }
        break;
    case MINIC_X86_MACHINE_FORMAT_RR:
    case MINIC_X86_MACHINE_FORMAT_IR:
    case MINIC_X86_MACHINE_FORMAT_AR:
    case MINIC_X86_MACHINE_FORMAT_RA:
    case MINIC_X86_MACHINE_FORMAT_FR:
    case MINIC_X86_MACHINE_FORMAT_RF:
    case MINIC_X86_MACHINE_FORMAT_SR:
        if ((ins->opcode == MINIC_X86_MI_MOVABS64_SYMBOL_R ||
             ins->opcode == MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R) &&
            fputs(" $",out) == EOF) return false;
        if (ins->opcode != MINIC_X86_MI_MOVABS64_SYMBOL_R &&
            ins->opcode != MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R &&
            fputc(' ',out) == EOF) return false;
        if (!x86_emit_typed_operand(out,function,&ins->operands[0],opcode))
            return false;
        if (opcode->format == MINIC_X86_MACHINE_FORMAT_SR &&
            ins->opcode != MINIC_X86_MI_MOVABS64_SYMBOL_R &&
            ins->opcode != MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R &&
            fputs("(%rip)",out) == EOF) return false;
        if (fputs(", ",out) == EOF ||
            !x86_emit_typed_operand(out,function,&ins->operands[1],opcode))
            return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_R:
        if (ins->opcode == MINIC_X86_MI_JMP_R ||
            ins->opcode == MINIC_X86_MI_CALL_SYSV_R ||
            ins->opcode == MINIC_X86_MI_CALL_MS_R) {
            if (fputs(" *",out) == EOF) return false;
        } else if (fputc(' ',out) == EOF) return false;
        if (ins->opcode == MINIC_X86_MI_SHL64_CL_R ||
            ins->opcode == MINIC_X86_MI_SHR64_CL_R ||
            ins->opcode == MINIC_X86_MI_SAR64_CL_R ||
            ins->opcode == MINIC_X86_MI_SHL32_CL_R ||
            ins->opcode == MINIC_X86_MI_SHR32_CL_R ||
            ins->opcode == MINIC_X86_MI_SAR32_CL_R) {
            if (fputs("%cl, ",out) == EOF) return false;
        }
        if (!x86_emit_gp_operand(out,&ins->operands[0])) return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_B:
        if (ins->operands[0].value.block_id >= labels->count ||
            labels->names == NULL ||
            labels->names[ins->operands[0].value.block_id] == NULL ||
            fprintf(out," %s",
                labels->names[ins->operands[0].value.block_id]) < 0)
            return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_S:
        if (fputc(' ',out) == EOF ||
            !x86_emit_typed_operand(out,function,&ins->operands[0],opcode))
            return false;
        break;
    case MINIC_X86_MACHINE_FORMAT_A:
    case MINIC_X86_MACHINE_FORMAT_F:
        if (fputc(' ',out) == EOF ||
            !x86_emit_typed_operand(out,function,&ins->operands[0],opcode))
            return false;
        break;
    default: return false;
    }
    return fputc('\n',out) != EOF;
}

bool minic_x86_64_machine_gnu_asm_policy(const MinicTargetInfo *target,
    MinicMachineGNUAsmPolicy *out) {
    if (target == NULL || target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        out == NULL) return false;
    out->verify.lookup_opcode=minic_x86_64_machine_opcode_lookup;
    out->verify.validate_selected_instruction=
        minic_x86_64_machine_validate_selected;
    out->verify.validate_generated_features=
        minic_x86_64_machine_validate_features;
    out->verify.validate_function_abi=minic_x86_64_machine_validate_abi;
    out->verify.validate_frame_object=
        minic_x86_64_machine_validate_frame_object;
    out->verify.validate_register=minic_x86_64_machine_validate_register;
    out->verify.validate_address=minic_x86_64_machine_validate_address;
    out->verify.validate_relocation=minic_x86_64_machine_validate_relocation;
    out->verify.validate_source_asm=minic_x86_64_machine_validate_source_asm;
    out->verify.context=NULL;
    out->emit_selected=x86_emit_selected;
    out->emit_source_asm=x86_emit_source_asm;
    out->context=NULL;
    return true;
}
