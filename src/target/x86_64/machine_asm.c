#include "target/x86_64/machine_internal.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/asm_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Target-only fixed-register GNU asm emitter. Core operands are independent
 * scalar SSA values and address-backed lvalues, never compiler-private C
 * pseudo-registers. Only prequalified complete register contracts are emitted. */
static bool clobber_is(const MinicCoreInlineAsmRegisterClobber *c,
                       const char *name) {
    const size_t len = strlen(name);
    return c->name && c->name_length == len &&
           memcmp(c->name, name, len) == 0;
}

/* Resolve a GNU `i` function-symbol operand exclusively from its Core SSA
 * definition. The target must emit CALL rel32 with a direct ELF relocation,
 * not load a runtime function pointer, invent a C call, or consult the AST. */
static const char *x86_asm_direct_function_symbol(
    const MinicCoreFunction *fn, MinicCoreValueId value) {
    const MinicCoreInstruction *definition;
    size_t symbol_id;
    if (!fn || value >= fn->value_count ||
        fn->values[value].definition >= fn->instruction_count) return NULL;
    definition = &fn->instructions[fn->values[value].definition];
    if (definition->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
        definition->result != value) return NULL;
    symbol_id = definition->value.function_symbol_id;
    if (symbol_id >= fn->function_symbol_count ||
        !fn->function_symbols[symbol_id].name ||
        !fn->function_symbols[symbol_id].name[0]) return NULL;
    return fn->function_symbols[symbol_id].name;
}

/* Linux's task-register operation consumes the *low selector word*. The
 * upper bits of GNU's q-class input must never become the segment selector. */
static bool emit_linux_task_control(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = minic_x86_64_linux_task_control_form(
        info->template_text, info->template_length);
    if (!out || !fn || !frame || !ins || !info || !b ||
        (form != 1U && form != 2U) ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        b[0].operand_index || b[0].value >= fn->value_count ||
        b[0].kind != (form == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        b[0].early_clobber || b[0].has_fixed_register_binding ||
        (form == 1U ? !minic_type_is_integer(fn->values[b[0].value].type)
                    : !minic_type_is_pointer(fn->values[b[0].value].type)))
        return false;
    if (form == 1U)
        return x86_value_load(out, frame, b[0].value, "%rax") &&
               fputs("  ltr %ax\n", out) != EOF;
    return fputs("  str %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}

/* IRETQ reloads the live SS:SP, FLAGS and CS:IP, providing the serializing
 * instruction boundary required by sync_core(). GNU binds the otherwise
 * immutable current_stack_pointer to physical RSP, not an SSA stack slot. */
static bool emit_linux_iret_to_self(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *fixed;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_iret_to_self_form(info->template_text,
                                              info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U ||
        !clobber_is(&info->register_clobbers[0], "cc") ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].has_fixed_register_binding ||
        !b[1].has_fixed_register_binding ||
        b[1].fixed_register_binding_id >= fn->fixed_register_binding_count ||
        !minic_type_is_pointer(fn->values[b[0].value].type) ||
        !minic_type_equal(fn->values[b[1].value].type, minic_type_unsigned_long()))
        return false;
    fixed = &fn->fixed_register_bindings[b[1].fixed_register_binding_id];
    if (!fixed->register_name || strcmp(fixed->register_name, "rsp")) return false;
    return fputs(
        "  xorl %r11d, %r11d\n"
        "  movw %ss, %r11w\n"
        "  pushq %r11\n"
        "  pushq %rsp\n"
        "  addq $8, (%rsp)\n"
        "  pushfq\n"
        "  xorl %r11d, %r11d\n"
        "  movw %cs, %r11w\n"
        "  pushq %r11\n"
        "  pushq $1f\n"
        "  iretq\n"
        "1:\n", out) != EOF &&
        x86_value_load(out, frame, b[0].value, "%r10") &&
        fputs("  movl %r11d, (%r10)\n", out) != EOF;
}

/* Preserve the exception entry address and objtool's ENDBR exclusion. RDI
 * points at the self-test counter and is the actual GNU D-class operand. */
static bool emit_linux_int3_selftest(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *fixed;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_int3_selftest_form(info->template_text,
                                               info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !b[0].has_fixed_register_binding ||
        b[0].fixed_register_binding_id >= fn->fixed_register_binding_count ||
        b[1].has_fixed_register_binding || b[0].early_clobber || b[1].early_clobber ||
        !minic_type_is_pointer(fn->values[b[1].value].type)) return false;
    fixed = &fn->fixed_register_bindings[b[0].fixed_register_binding_id];
    if (!fixed->register_name || strcmp(fixed->register_name, "rsp")) return false;
    return x86_value_load(out, frame, b[1].value, "%rdi") &&
           fputs("int3_selftest_ip:\n986:\n"
                 "  .pushsection .discard.noendbr\n"
                 "  .long 986b\n"
                 "  .popsection\n"
                 "  int3\n  nop\n  nop\n  nop\n  nop\n", out) != EOF;
}

static bool emit_linux_real_mode_far_jump(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_real_mode_far_jump_form(info->template_text,
                                                    info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].early_clobber || b[1].early_clobber ||
        !minic_type_is_pointer(fn->values[b[0].value].type) ||
        !minic_type_is_integer(fn->values[b[1].value].type)) return false;
    /* The operand is the address of the 6-byte m16:32 memory far pointer;
     * loading *that address* is required, loading its 32-bit first word or
     * issuing an ordinary near JMP would corrupt the restart transition. */
    return x86_value_load(out, frame, b[1].value, "%rdi") &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  ljmpl *(%r11)\n", out) != EOF;
}

/* Linux's paravirt patch site contains the actual indirect memory CALL.
 * Keeping it as a plain direct call, an SSA function pointer or an omitted
 * .parainstructions relocation would break runtime patching. The instruction
 * byte range and site address are materialized by the target assembler. */
static bool emit_linux_pv_indirect_call(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const outputs[] = {"%rdi", "%rsi", "%rdx", "%rcx"};
    static const char *const clobbers[] = {"cc", "rax", "r8", "r9", "r10", "r11"};
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *fixed;
    const MinicCoreInstruction *constant;
    int64_t type_number;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_pv_indirect_call_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 6U || !info->register_clobbers ||
        ins->value.structured_inline_asm.operand_count != 7U) return false;
    for (i = 0U; i < 6U; ++i)
        if (!clobber_is(&info->register_clobbers[i], clobbers[i])) return false;
    for (i = 0U; i < 7U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber ||
            b[i].kind != (i < 4U
                             ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                             : i == 6U
                                   ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                                   : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            b[i].has_fixed_register_binding != (i == 4U)) return false;
    }
    if (b[4].fixed_register_binding_id >= fn->fixed_register_binding_count)
        return false;
    fixed = &fn->fixed_register_bindings[b[4].fixed_register_binding_id];
    if (!fixed->register_name || strcmp(fixed->register_name, "rsp") ||
        !minic_type_equal(fn->values[b[4].value].type, minic_type_unsigned_long()) ||
        !minic_type_is_integer(fn->values[b[5].value].type) ||
        !minic_type_is_pointer(fn->values[b[6].value].type) ||
        fn->values[b[5].value].definition >= fn->instruction_count) return false;
    constant = &fn->instructions[fn->values[b[5].value].definition];
    if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        constant->result != b[5].value) return false;
    type_number = constant->value.integer_value;
    if (type_number < 0 || type_number > 255) return false;
    if (!x86_value_load(out, frame, b[6].value, "%r11") ||
        fputs("771:\n999:\n"
              "  .pushsection .discard.retpoline_safe\n"
              "  .long 999b\n"
              "  .popsection\n"
              "  call *(%r11)\n"
              "772:\n"
              "  .pushsection .parainstructions,\"a\"\n"
              "  .balign 8\n"
              "  .quad 771b\n", out) == EOF ||
        fprintf(out, "  .byte %lld\n", (long long)type_number) < 0 ||
        fputs("  .byte 772b - 771b\n"
              "  .balign 8\n"
              "  .popsection\n", out) == EOF) return false;
    for (i = 0U; i < 4U; ++i)
        if (!x86_value_load(out, frame, b[i].value, "%r10") ||
            fprintf(out, "  movq %s, (%%r10)\n", outputs[i]) < 0) return false;
    return true;
}

/* Replacement bytes execute *later*, after kernel alternative patching.
 * Therefore the address operand must stay a RIP-relative symbol relocation
 * inside the replacement fragment: a preloaded register is not equivalent. */
static bool emit_linux_alt_reloc_selftest(
    FILE *out, const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *definition;
    const char *name;
    if (!out || !fn || !ins || !info || !b ||
        !minic_x86_64_linux_alt_reloc_selftest_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 1U ||
        !clobber_is(&info->register_clobbers[0], "rdi") ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        b[0].operand_index != 0U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].value >= fn->value_count || b[0].has_fixed_register_binding ||
        fn->values[b[0].value].definition >= fn->instruction_count) return false;
    definition = &fn->instructions[fn->values[b[0].value].definition];
    if (definition->kind != MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS ||
        definition->result != b[0].value ||
        definition->value.global_id >= fn->global_count) return false;
    name = fn->globals[definition->value.global_id].name;
    if (!name || strcmp(name, "__alt_reloc_selftest_addr") != 0) return false;
    return fputs(
        "661:\n"
        "662:\n"
        "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .long 661b - .\n"
        "  .long 6641f - .\n"
        "  .4byte (3*32+21)\n"
        "  .byte 663b-661b\n"
        "  .byte 6651f-6641f\n"
        "  .popsection\n"
        "  .pushsection .altinstr_replacement,\"ax\"\n"
        "6641:\n"
        "  leaq __alt_reloc_selftest_addr(%rip), %rdi\n"
        "  call __alt_reloc_selftest\n"
        "6651:\n"
        "  .popsection\n", out) != EOF;
}

static bool emit_linux_mmio_read(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    const unsigned int width = info ? minic_x86_64_linux_mmio_read_form(
        info->template_text, info->template_length) : 0U;
    if (!out || !fn || !frame || !ins || !info || !b || !width ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !minic_type_unqualified(pointee, &pointee) ||
        !minic_x86_64_linux_mmio_type_matches(pointee, width) ||
        !minic_type_pointee(fn->values[b[1].value].type, &pointee) ||
        !minic_type_unqualified(pointee, &pointee) ||
        !minic_x86_64_linux_mmio_type_matches(pointee, width)) return false;
    return x86_value_load(out, frame, b[1].value, "%r11") &&
           fputs(width == 1U ? "  movzbl (%r11), %eax\n" :
                 width == 2U ? "  movzwl (%r11), %eax\n" :
                 width == 4U ? "  movl (%r11), %eax\n" :
                               "  movq (%r11), %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r10") &&
           fputs(width == 1U ? "  movb %al, (%r10)\n" :
                 width == 2U ? "  movw %ax, (%r10)\n" :
                 width == 4U ? "  movl %eax, (%r10)\n" :
                               "  movq %rax, (%r10)\n", out) != EOF;
}

static bool emit_linux_mmio_write(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    const unsigned int width = info ? minic_x86_64_linux_mmio_write_form(
        info->template_text, info->template_length) : 0U;
    if (!out || !fn || !frame || !ins || !info || !b || !width ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_x86_64_linux_mmio_type_matches(fn->values[b[0].value].type, width) ||
        !minic_type_pointee(fn->values[b[1].value].type, &pointee) ||
        !minic_type_unqualified(pointee, &pointee) ||
        !minic_x86_64_linux_mmio_type_matches(pointee, width)) return false;
    return x86_value_load(out, frame, b[0].value, "%rax") &&
           x86_value_load(out, frame, b[1].value, "%r11") &&
           fputs(width == 1U ? "  movb %al, (%r11)\n" :
                 width == 2U ? "  movw %ax, (%r11)\n" :
                 width == 4U ? "  movl %eax, (%r11)\n" :
                               "  movq %rax, (%r11)\n", out) != EOF;
}

static bool emit_linux_rep_string_io(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const opcodes[] = {
        "rep outsb", "rep outsw", "rep outsl",
        "rep insb", "rep insw", "rep insl"
    };
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    const unsigned int form = info ? minic_x86_64_linux_rep_string_io_form(
        info->template_text, info->template_length) : 0U;
    if (!out || !fn || !frame || !ins || !info || !b || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[2].operand_index != 2U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
        b[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].early_clobber || b[1].early_clobber || b[2].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[2].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[2].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !minic_type_is_pointer(pointee) ||
        !minic_type_pointee(fn->values[b[1].value].type, &pointee) ||
        !minic_type_equal(pointee, minic_type_unsigned_long()) ||
        !minic_type_equal(fn->values[b[2].value].type,
                          minic_type_unsigned_short())) return false;
    /* The fixed-register C operand is a pointer to the source C lvalue,
     * not the value of that lvalue. Load SI/DI/CX before the instruction and
     * write back both architecturally updated registers afterward. */
    if (!x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs(form <= 3U ? "  movq (%r11), %rsi\n" :
                           "  movq (%r11), %rdi\n", out) == EOF ||
        !x86_value_load(out, frame, b[1].value, "%r11") ||
        fputs("  movq (%r11), %rcx\n", out) == EOF ||
        !x86_value_load(out, frame, b[2].value, "%rdx") ||
        fprintf(out, "  %s\n", opcodes[form - 1U]) < 0 ||
        !x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs(form <= 3U ? "  movq %rsi, (%r11)\n" :
                           "  movq %rdi, (%r11)\n", out) == EOF ||
        !x86_value_load(out, frame, b[1].value, "%r11") ||
        fputs("  movq %rcx, (%r11)\n", out) == EOF) return false;
    return true;
}

/* Linux iomem's three exact GNU string-copy shapes use fixed SI/DI/CX
   registers and tied input operands.  Preserve architectural pointer
   increments as observable C outputs; no compiler-generated memcpy call. */
static bool emit_linux_iomem_copy(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = info ? minic_x86_64_linux_iomem_copy_form(
        info->template_text, info->template_length) : 0U;
    const size_t outputs = form == 3U ? 3U : 2U;
    const size_t inputs = form == 3U ? 4U : 2U;
    size_t i;

    if (!out || !fn || !frame || !ins || !info || !form || !b ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != outputs + inputs)
        return false;
    for (i = 0U; i < outputs + inputs; ++i) {
        MinicType pointee;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i < outputs
                 ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                 : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
        if (i < outputs &&
            (!minic_type_pointee(fn->values[b[i].value].type, &pointee) ||
            (form == 3U ? !minic_type_is_integer(pointee) :
                          !minic_type_is_pointer(pointee)))) return false;
        if (i >= outputs &&
            !minic_type_is_integer(fn->values[b[i].value].type) &&
            !minic_type_is_pointer(fn->values[b[i].value].type)) return false;
    }
    if (form == 3U) {
        if (!x86_value_load(out, frame, b[3].value, "%rcx") ||
            !x86_value_load(out, frame, b[5].value, "%rdi") ||
            !x86_value_load(out, frame, b[6].value, "%rsi") ||
            !x86_value_load(out, frame, b[4].value, "%rax") ||
            fputs("  rep movsl\n"
                  "  testb $2, %al\n"
                  "  je 1f\n"
                  "  movsw\n"
                  "1: testb $1, %al\n"
                  "  je 2f\n"
                  "  movsb\n"
                  "2:\n", out) == EOF) return false;
    } else {
        if (!x86_value_load(out, frame, b[2].value, "%rdi") ||
            !x86_value_load(out, frame, b[3].value, "%rsi") ||
            fputs(form == 1U ? "  movsb\n" : "  movsw\n", out) == EOF)
            return false;
    }
    for (i = 0U; i < outputs; ++i) {
        const char *reg = form == 3U
            ? (i == 0U ? "%rcx" : (i == 1U ? "%rdi" : "%rsi"))
            : (i == 0U ? "%rdi" : "%rsi");
        if (!x86_value_load(out, frame, b[i].value, "%r11") ||
            fprintf(out, "  movq %s, (%%r11)\n", reg) < 0) return false;
    }
    return true;
}

static bool emit_linux_i915_movntdqa(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = info ? minic_x86_64_linux_i915_movntdqa_form(
        info->template_text, info->template_length) : 0U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const bool four = form >= 3U;
    const char *store = form == 1U || form == 3U ? "movaps" : "movups";
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !form ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    for (i = 0U; i < 2U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            !minic_type_is_pointer(fn->values[b[i].value].type))
            return false;
    if (!x86_value_load(out, frame, b[0].value, "%rax") ||
        !x86_value_load(out, frame, b[1].value, "%r10"))
        return false;
    /* The kernel's kernel_fpu_begin() owns the XMM context. MOVNTDQA is
     * SSE4.1 and Linux guards the call with X86_FEATURE_XMM4_1; here we
     * preserve exactly the guarded hardware instruction, not memcpy. */
    for (i = 0U; i < (four ? 4U : 1U); ++i)
        if (fprintf(out,
                    "  movntdqa %zu(%%rax), %%xmm%zu\n",
                    i * 16U, i) < 0)
            return false;
    for (i = 0U; i < (four ? 4U : 1U); ++i)
        if (fprintf(out,
                    "  %s %%xmm%zu, %zu(%%r10)\n",
                    store, i, i * 16U) < 0)
            return false;
    return true;
}

static bool emit_linux_flushcache_copy(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = info ? minic_x86_64_linux_flushcache_copy_form(
        info->template_text, info->template_length) : 0U;
    const size_t count = 2U;
    size_t i;

    if (!out || !fn || !frame || !ins || !info || !b || !form ||
        info->is_goto || !info->is_volatile ||
        !info->has_memory_clobber ||
        ins->value.structured_inline_asm.operand_count != count ||
        info->register_clobber_count != (form == 4U ? 0U : (form == 1U ? 4U : 1U)))
        return false;
    for (i = 0U; i < count; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (form == 4U && i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
    }
    if (form == 4U) {
        size_t index;

        if (!minic_type_is_pointer(fn->values[b[1].value].type) ||
            !x86_value_load(out, frame, b[1].value, "%rax")) return false;
        /* The compiler-normalized original `clwb` ALTERNATIVE owns both
           .altinstructions entries and the replacement section. Replace
           only its target-qualified `[pax]` operand with fixed RAX; never
           discard feature metadata or rewrite the replacement bytes. */
        for (index = 0U; index < info->template_length; ++index) {
            if (info->template_text[index] == '%' &&
                index + 1U < info->template_length &&
                info->template_text[index + 1U] == '1') {
                if (fputs("%rax", out) == EOF) return false;
                index += 1U;
            } else if (fputc(info->template_text[index], out) == EOF) {
                return false;
            }
        }
        return fputc('\n', out) != EOF;
    }
    if (!minic_type_is_integer(fn->values[b[0].value].type) ||
        !minic_type_is_integer(fn->values[b[1].value].type) ||
        !x86_value_load(out, frame, b[0].value, "%rax") ||
        !x86_value_load(out, frame, b[1].value, "%rcx")) return false;
    if (form == 1U)
        return fputs("  movq (%rax), %r8\n"
                     "  movq 8(%rax), %r9\n"
                     "  movq 16(%rax), %r10\n"
                     "  movq 24(%rax), %r11\n"
                     "  movnti %r8, (%rcx)\n"
                     "  movnti %r9, 8(%rcx)\n"
                     "  movnti %r10, 16(%rcx)\n"
                     "  movnti %r11, 24(%rcx)\n", out) != EOF;
    if (form == 2U)
        return fputs("  movq (%rax), %r8\n"
                     "  movnti %r8, (%rcx)\n", out) != EOF;
    return fputs("  movl (%rax), %r8d\n"
                 "  movnti %r8d, (%rcx)\n", out) != EOF;
}

/* Exchange has an implicit LOCK when its destination is memory. The +r
 * output holds the old memory value and the +m output is updated with the
 * source register; both are address-backed writable C operands. */
static bool emit_linux_atomic_xchg(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const instructions[] = {
        "  xchgb %al, (%r10)\n", "  xchgw %ax, (%r10)\n",
        "  xchgl %eax, (%r10)\n", "  xchgq %rax, (%r10)\n"
    };
    static const char *const loads[] = {
        "  movzbl (%r11), %eax\n", "  movzwl (%r11), %eax\n",
        "  movl (%r11), %eax\n", "  movq (%r11), %rax\n"
    };
    static const char *const stores[] = {
        "  movb %al, (%r11)\n", "  movw %ax, (%r11)\n",
        "  movl %eax, (%r11)\n", "  movq %rax, (%r11)\n"
    };
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int width = info ? minic_x86_64_linux_atomic_xchg_form(
        info->template_text, info->template_length) : 0U;
    const size_t index = width == 1U ? 0U : width == 2U ? 1U :
                         width == 4U ? 2U : 3U;
    MinicType reg_type, mem_type;
    size_t reg_bytes, mem_bytes;
    bool reg_signed, mem_signed;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        !b || !width || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 1U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &reg_type) ||
        !minic_type_pointee(fn->values[b[1].value].type, &mem_type) ||
        !minic_x86_64_scalar_width(program, target, reg_type,
                                     &reg_bytes, &reg_signed) ||
        !minic_x86_64_scalar_width(program, target, mem_type,
                                     &mem_bytes, &mem_signed) ||
        reg_bytes != width || mem_bytes != width) return false;
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs(loads[index], out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs(instructions[index], out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs(stores[index], out) != EOF;
}

static bool emit_linux_pvclock_scale(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_pvclock_scale_form(info->template_text,
                                               info->template_length) ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U) return false;
    for (i = 0U; i < 4U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i < 2U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
    if (!minic_type_is_pointer(fn->values[b[0].value].type) ||
        !minic_type_is_pointer(fn->values[b[1].value].type) ||
        !minic_type_is_integer(fn->values[b[2].value].type) ||
        !minic_type_is_integer(fn->values[b[3].value].type)) return false;
    return x86_value_load(out, frame, b[2].value, "%rax") &&
           x86_value_load(out, frame, b[3].value, "%r11") &&
           fputs("  mulq %r11\n  shrdq $32, %rdx, %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r10") &&
           fputs("  movq %rax, (%r10)\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs("  movq %rdx, (%r10)\n", out) != EOF;
}

/* The three ISA alternatives share the same RDI pointer operand. Linux's
 * `+r(current_stack_pointer)` is the physical RSP, invariant across each
 * returning CALL; machine code must never move a synthetic SSA value to
 * RSP. Each replacement is a genuine five-byte CALL rel32. All feature
 * identifiers and .altinstructions PC-relative metadata are emitted by the
 * target and are independently verified by GNU AS in the Linux fixture. */
static bool emit_linux_preempt_static_call(
    FILE *out, const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_preempt_static_call_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *binding =
        &ins->value.structured_inline_asm.operands[0];
    const MinicCoreFixedRegisterBinding *fixed;
    if (!out || !fn || !ins || !info || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        binding->operand_index != 0U ||
        binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !binding->has_fixed_register_binding || binding->early_clobber ||
        binding->value >= fn->value_count ||
        binding->fixed_register_binding_id >= fn->fixed_register_binding_count ||
        !minic_type_equal(fn->values[binding->value].type,
                          minic_type_unsigned_long())) return false;
    fixed = &fn->fixed_register_bindings[binding->fixed_register_binding_id];
    if (!fixed->register_name || strcmp(fixed->register_name, "rsp") != 0)
        return false;
    /* The fixed RSP GNU +r constraint encodes a liveness barrier. A return
     * from the trampoline restores RSP, so emitting a synthetic read/write
     * store for this operand would itself corrupt the hardware stack. */
    return fputs(form == 1U ? "  call __SCT__preempt_schedule\n" :
                            "  call __SCT__preempt_schedule_notrace\n",
                 out) != EOF;
}

static bool emit_linux_irq_stack_call(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const expected_clobbers[] = {
        "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10"
    };
    const unsigned int form = minic_x86_64_linux_irq_stack_call_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const char *callee;
    const MinicCoreFixedRegisterBinding *fixed;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 9U || !info->register_clobbers ||
        ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 5U : form == 2U ? 6U : 4U)) return false;
    for (i = 0U; i < 9U; ++i)
        if (!clobber_is(&info->register_clobbers[i], expected_clobbers[i]))
            return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[i].early_clobber) return false;
        if (i == 0U || i == 1U || i == 3U) {
            static const char *const names[] = {"r11", "rsp", "r11"};
            const size_t register_index = i == 0U ? 0U : i == 1U ? 1U : 2U;
            if (!b[i].has_fixed_register_binding ||
                b[i].fixed_register_binding_id >= fn->fixed_register_binding_count)
                return false;
            fixed = &fn->fixed_register_bindings[b[i].fixed_register_binding_id];
            if (!fixed->register_name ||
                strcmp(fixed->register_name, names[register_index]) != 0)
                return false;
        } else if (b[i].has_fixed_register_binding) {
            return false;
        }
    }
    if (!minic_type_is_pointer(fn->values[b[0].value].type) ||
        !minic_type_equal(fn->values[b[1].value].type, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(fn->values[b[3].value].type) ||
        (form != 3U && !minic_type_is_pointer(fn->values[b[4].value].type)) ||
        (form == 2U && !minic_type_is_integer(fn->values[b[5].value].type)) ||
        !(callee = x86_asm_direct_function_symbol(fn, b[2].value))) return false;
    if (form == 3U) {
        /* softirq uses the same two fixed live RSP/R11 GNU bindings but
         * calls __do_softirq directly, without IRQ RCU wrappers. */
        return x86_value_load(out, frame, b[3].value, "%r11") &&
               fputs("  movq %rsp, (%r11)\n  movq %r11, %rsp\n",out)!=EOF &&
               fprintf(out,"  call %s\n",callee)>=0 &&
               fputs("998:\n"
                     "  .pushsection .discard.reachable\n"
                     "  .long 998b\n  .popsection\n"
                     "  popq %rsp\n",out)!=EOF;
    }
    /* Save callee-saved operands on the original C stack. Once the incoming
     * IRQ-stack top is installed in RSP, its first word retains this exact
     * saved RSP; popq %rsp switches back after all three CALLs. The two
     * pushes preserve original 16-byte call alignment, and R12/R13 survive
     * the irq_enter_rcu/irq_exit_rcu helper calls. */
    if (fputs("  pushq %r12\n  pushq %r13\n", out) == EOF ||
        !x86_value_load(out, frame, b[4].value, "%r12") ||
        (form == 2U && !x86_value_load(out, frame, b[5].value, "%r13")) ||
        !x86_value_load(out, frame, b[3].value, "%r11") ||
        fputs("  movq %rsp, (%r11)\n"
              "  movq %r11, %rsp\n"
              "  call irq_enter_rcu\n"
              "  movq %r12, %rdi\n", out) == EOF ||
        (form == 2U && fputs("  movq %r13, %rsi\n", out) == EOF) ||
        fprintf(out, "  call %s\n", callee) < 0 ||
        fputs("998:\n"
              "  .pushsection .discard.reachable\n"
              "  .long 998b\n"
              "  .popsection\n"
              "  call irq_exit_rcu\n"
              "  popq %rsp\n"
              "  popq %r13\n"
              "  popq %r12\n", out) == EOF) return false;
    return true;
}

static bool emit_linux_three_call_alternative(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *fixed;
    const char *skip, *skip_end;
    const char *names[3];
    MinicType output_type;
    size_t i, width;
    bool signed_type;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        !b || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 3U ||
        !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "cc") ||
        !clobber_is(&info->register_clobbers[1], "rax") ||
        !clobber_is(&info->register_clobbers[2], "rcx") ||
        !minic_x86_64_linux_three_call_alternative_form(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 6U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !b[1].has_fixed_register_binding ||
        b[1].fixed_register_binding_id >= fn->fixed_register_binding_count ||
        b[5].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].value >= fn->value_count || b[5].value >= fn->value_count ||
        b[0].has_fixed_register_binding || b[5].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_is_pointer(output_type) ||
        !minic_x86_64_scalar_width(
            program, target, output_type, &width, &signed_type) || width != 8U ||
        !minic_type_equal(fn->values[b[5].value].type, output_type))
        return false;
    fixed = &fn->fixed_register_bindings[b[1].fixed_register_binding_id];
    if (!fixed->register_name || fixed->register_name_length != 3U ||
        memcmp(fixed->register_name, "rsp", 3U) != 0 ||
        !minic_type_is_integer(fixed->type)) return false;
    for (i = 0U; i < 6U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || (i != 1U && b[i].has_fixed_register_binding))
            return false;
        if (i >= 2U && i <= 4U) {
            if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                !(names[i - 2U] = x86_asm_direct_function_symbol(fn, b[i].value)))
                return false;
        }
    }
    if (!x86_value_load(out, frame, b[5].value, "%rdi")) return false;
    /* The complete .skip arithmetic was equality-checked against the
     * exact Linux grammar, including both MAX operands. Preserve those
     * original assembler tokens without reproducing a subtly different
     * parenthesization in a second copy of the expression. */
    skip = strstr(info->template_text, ".skip ");
    skip_end = skip ? strchr(skip, '\n') : NULL;
    if (!skip || !skip_end || skip_end <= skip ||
        fprintf(out, "661:\n  call %s\n662:\n", names[0]) < 0 ||
        fwrite(skip, 1U, (size_t)(skip_end - skip + 1), out) !=
            (size_t)(skip_end - skip + 1)) return false;
    if (fprintf(out,
         "663:\n"
         "  .pushsection .altinstructions,\"a\"\n"
         "  .long 661b - .\n"
         "  .long 6641f - .\n"
         "  .4byte (3*32+16)\n"
         "  .byte 663b-661b\n"
         "  .byte 6651f-6641f\n"
         "  .long 661b - .\n"
         "  .long 6642f - .\n"
         "  .4byte (9*32+9)\n"
         "  .byte 663b-661b\n"
         "  .byte 6652f-6642f\n"
         "  .popsection\n"
         "  .pushsection .altinstr_replacement,\"ax\"\n"
         "6641:\n"
         "  call %s\n"
         "6651:\n"
         "6642:\n"
         "  call %s\n"
         "6652:\n"
         "  .popsection\n",
         names[1], names[2]) < 0) return false;
    /* GNU `=D` writes the post-CALL RDI to the C lvalue. The call can
     * advance the pointer (REP STOS), so dropping this store is incorrect. */
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rdi, (%r11)\n", out) != EOF;
}

/* Control registers are architectural registers, not ordinary Core scalar
 * lvalues. The selected ISA owns both the encoding and the read/write
 * direction. The read's GNU dummy memory operand is an ordering dependency;
 * emitting a load from its synthetic address (often 0x1000) would fault. */
static bool emit_linux_control_register(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const machine_registers[] = {
        "%cr0", "%cr2", "%cr3", "%cr4", "%cr2", "%cr3", "%cr0", "%cr4"
    };
    const unsigned int form = minic_x86_64_linux_control_register_form(
        info->template_text, info->template_length);
    const bool reading = form != 0U && form <= 4U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const bool plain_read = reading &&
        ins->value.structured_inline_asm.operand_count == 1U;
    const bool plain_write = (form == 7U || form == 8U) &&
        ins->value.structured_inline_asm.operand_count == 1U &&
        b[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    MinicType output_type, input_type;
    size_t width, i;
    bool sign;
    if (!form || !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        (!info->has_memory_clobber && !plain_read && !plain_write) ||
        ins->value.structured_inline_asm.operand_count !=
            (reading && !plain_read ? 2U : 1U))
        return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i)
        if (b[i].value >= fn->value_count || b[i].operand_index != i ||
            b[i].early_clobber || b[i].has_fixed_register_binding) return false;
    if (reading) {
        if (b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            (!plain_read &&
             b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
            !minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
            (!plain_read &&
             !minic_type_pointee(fn->values[b[1].value].type, &input_type)) ||
            !minic_type_equal(output_type, minic_type_unsigned_long()) ||
            (!plain_read &&
             !minic_type_equal(input_type, minic_type_unsigned_int())) ||
            !minic_x86_64_scalar_width(
                program, target, output_type, &width, &sign) || width != 8U)
            return false;
        return fprintf(out, "  movq %s, %%rax\n",
                       machine_registers[form - 1U]) >= 0 &&
               x86_value_load(out, frame, b[0].value, "%r11") &&
               fputs("  movq %rax, (%r11)\n", out) != EOF;
    }
    if ((form == 7U || form == 8U) && !plain_write) {
        if (b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
            !minic_type_pointee(fn->values[b[0].value].type, &input_type) ||
            !minic_type_equal(input_type, minic_type_unsigned_long()) ||
            !x86_value_load(out, frame, b[0].value, "%r11")) return false;
        /* GNU +r input/output: source is live and remains unchanged after
         * moving its bits to the privileged control register. */
        return fprintf(out, "  movq (%%r11), %%rax\n  movq %%rax, %s\n",
                       machine_registers[form - 1U]) >= 0;
    }
    if (b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !minic_type_equal(fn->values[b[0].value].type, minic_type_unsigned_long()))
        return false;
    return x86_value_load(out, frame, b[0].value, "%rax") &&
           fprintf(out, "  movq %%rax, %s\n",
                   machine_registers[form - 1U]) >= 0;
}

static bool emit_linux_tlb_cache(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_tlb_cache_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    if (!form || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        b[0].operand_index != 0U || b[0].value >= fn->value_count ||
        b[0].early_clobber || b[0].has_fixed_register_binding ||
        b[0].kind != (form == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
        !(minic_type_is_pointer(fn->values[b[0].value].type) ||
          (form == 1U && minic_type_equal(
              fn->values[b[0].value].type, minic_type_unsigned_long()))) ||
        !x86_value_load(out, frame, b[0].value, "%rax")) return false;
    /* Distinct cache/TLB hardware effects with exact GNU operand grammar.
     * `clflush` uses the checked +m C lvalue; `invlpg` takes an address. */
    return fputs(form == 1U ? "  invlpg (%rax)\n" :
                                 "  clflush (%rax)\n", out) != EOF;
}

/* Linux's alternative_msr_write is not an unconditional WRMSR. The kernel
 * patches its old two-byte NOP area only on CPUs with the selected feature;
 * both the empty baseline and the replacement preserve the same live
 * ECX:EAX:EDX inputs. The feature number is an INTEGER_CONSTANT in Core,
 * never a runtime value smuggled into an ELF .4byte directive. */
static bool emit_linux_alternative_wrmsr(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *feature;
    static const char *const physical[] = {"%rcx", "%rax", "%rdx"};
    size_t i, width;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        !b || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 4U ||
        !minic_x86_64_linux_alternative_wrmsr_form(
            info->template_text, info->template_length)) return false;
    for (i = 0U; i < 4U; ++i) {
        if (b[i].operand_index != i ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[i].value >= fn->value_count || b[i].early_clobber ||
            b[i].has_fixed_register_binding ||
            !minic_x86_64_scalar_width(
                program, target, fn->values[b[i].value].type,
                &width, &sign) || (i < 3U && width != 4U) ||
            (i == 3U && (width != 4U && width != 8U))) return false;
    }
    if (fn->values[b[3].value].definition >= fn->instruction_count)
        return false;
    feature = &fn->instructions[fn->values[b[3].value].definition];
    if (feature->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        feature->value.integer_value < 0 ||
        (uint64_t)feature->value.integer_value > UINT32_MAX) return false;
    for (i = 0U; i < 3U; ++i)
        if (!x86_value_load(out, frame, b[i].value, physical[i])) return false;
    return fprintf(out,
        "661:\n"
        "662:\n"
        "  .skip -(((6651f-6641f)-(662b-661b))>0)*"
                  "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .long 661b - .\n"
        "  .long 6641f - .\n"
        "  .4byte %u\n"
        "  .byte 663b-661b\n"
        "  .byte 6651f-6641f\n"
        "  .popsection\n"
        "  .pushsection .altinstr_replacement,\"ax\"\n"
        "6641:\n"
        "  wrmsr\n"
        "6651:\n"
        "  .popsection\n",
        (unsigned int)feature->value.integer_value) >= 0;
}

/* Print GNU numeric template references using x86 assigned physical regs.
 * Operand 7 is a genuine memory reference, never a value expression. */
static bool emit_mulacc_template(FILE *out, const MinicCoreInlineAsm *info) {
    size_t i;
    for (i = 0U; i < info->template_length; ++i) {
        if (info->template_text[i] != '%') {
            if (fputc(info->template_text[i], out) == EOF) return false;
            continue;
        }
        if (++i >= info->template_length) return false;
        switch (info->template_text[i]) {
        case '%': if (fputc('%', out) == EOF) return false; break;
        case '0': if (fputs("%r8", out) == EOF) return false; break;
        case '1': if (fputs("%r9", out) == EOF) return false; break;
        case '2': if (fputs("%rcx", out) == EOF) return false; break;
        case '7': if (fputs("(%r11)", out) == EOF) return false; break;
        default: return false;
        }
    }
    return fputc('\n', out) != EOF;
}


/* Register-only GNU "=r"/matching-digit group with memory clobber. The
 * template has no implicit registers, and Core's preflight restricts the
 * instructions to mov/add/adc/sub/sbb and %N registers. No per-BusyBox-function
 * rewrites: the same ABI operand assignment works for add, sub and mod. */
static bool emit_matched_register_template(
    FILE *out, const MinicCoreInlineAsm *info, size_t outputs) {
    static const char *const regs[] = {"%r8", "%r9", "%r10", "%rcx"};
    size_t i;
    for (i = 0U; i < info->template_length; ++i) {
        if (info->template_text[i] != '%') {
            if (fputc(info->template_text[i], out) == EOF) return false;
        } else {
            if (++i >= info->template_length ||
                info->template_text[i] < '0' ||
                (size_t)(info->template_text[i] - '0') >= outputs ||
                fputs(regs[(size_t)(info->template_text[i] - '0')], out) == EOF)
                return false;
        }
    }
    return fputc('\n', out) != EOF;
}

/* 64x64 unsigned multiply into RDX:RAX, then 128/64 unsigned divide.
 * Exact GNU "=a"/"a"/"rm"/"rm"/"rdx" operand ownership is checked again
 * against Core binding roles. No hidden register allocator policy escapes
 * the x86 machine emitter and the output still lands in its SSA lvalue. */
/* Core's structured ASM describes output address + scalar input, not
 * an instruction string to be forwarded. The machine rechecks the entire
 * x86 64-bit shape, chooses physical caller-scratch R10/R11/RAX, and emits
 * only verified nonzero-word BSF/BSR semantics. */
static bool emit_bit_scan_u64(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    bool reverse = false, signed_result;
    MinicType output_type;
    size_t width;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        info->register_clobber_count != 0U ||
        info->has_memory_clobber || info->is_goto ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b == NULL ||
        !minic_x86_64_asm_bit_scan_template(
            info->template_text, info->template_length, &reverse) ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_unsigned_long()) ||
        !minic_type_equal(fn->values[b[1].value].type, minic_type_unsigned_long()) ||
        !minic_x86_64_scalar_width(
            program, target, output_type, &width, &signed_result) ||
        width != 8U)
        return false;
    return x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs(reverse ? "  bsrq %r10, %rax\n" :
                          "  rep; bsfq %r10, %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}

/* Kernel fls/variable_ffs use a third GNU tied input ("0"(-1))
 * with 32-bit BSF/BSR; fls64 uses one +r initialized 32-bit destination
 * with a 64-bit BSRQ. Respect input zero fallback and REAL flags, never
 * replace the hardware bit scan with a host C builtin. */
static bool emit_linux_tied_bit_scan(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    const MinicX86BitScanForm form = minic_x86_64_asm_bit_scan_form(
        info->template_text, info->template_length);
    const bool is64 = form == MINIC_X86_BIT_SCAN_BSR64_READWRITE;
    const char *mnemonic = form == MINIC_X86_BIT_SCAN_BSF32_TIED ?
        "  bsfl %r10d, %eax\n" :
        form == MINIC_X86_BIT_SCAN_BSR32_TIED ?
        "  bsrl %r10d, %eax\n" : "  bsrq %r10, %rax\n";
    MinicType output_type;
    size_t width, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !info || !b ||
        (count != 3U && count != 2U) ||
        (is64 && count != 2U) || (!is64 && count != 3U) ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count || !info->template_text ||
        (form != MINIC_X86_BIT_SCAN_BSF32_TIED &&
         form != MINIC_X86_BIT_SCAN_BSR32_TIED &&
         form != MINIC_X86_BIT_SCAN_BSR64_READWRITE))
        return false;
    for (i = 0U; i < count; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                (is64 ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_int()) ||
        !minic_x86_64_scalar_width(program, target, output_type, &width, &sign) ||
        width != 4U ||
        !minic_x86_64_scalar_width(
            program, target, fn->values[b[1].value].type, &width, &sign) ||
        width != (is64 ? 8U : 4U))
        return false;
    if (count == 3U &&
        (!minic_type_equal(fn->values[b[2].value].type, minic_type_int()) ||
         !x86_value_load(out, frame, b[2].value, "%rax")))
        return false;
    if (is64) {
        if (!x86_value_load(out, frame, b[0].value, "%r11") ||
            fputs("  movl (%r11), %eax\n", out) == EOF)
            return false;
    }
    if (!x86_value_load(out, frame, b[1].value, "%r10") ||
        fputs(mnemonic, out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r11"))
        return false;
    return fputs("  movl %eax, (%r11)\n", out) != EOF;
}

/* One x86 hardware bitops emitter owns all source-validated
 * LOCK/non-LOCK BT/BTS/BTR/BTC, carrier CF and SMP lock patch metadata.
 * No template text is forwarded to GAS; only safe ISA rows emit machine
 * opcodes. GNU 'm' inputs can be real memory effects for BTS/BTR/BTC. */
static bool emit_linux_bitops(
    FILE *out,const MinicC0Program *program,const MinicTargetInfo *target,
    const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicX86BitopsForm form=minic_x86_64_linux_bitops_form(
        info->template_text,info->template_length);
    const MinicX86BitopsDesc *d=minic_x86_64_bitops_description(form);
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const size_t count=ins->value.structured_inline_asm.operand_count;
    const bool byte_rmw=form==MINIC_X86_BITOPS_LOCK_OR_BYTE||
                        form==MINIC_X86_BITOPS_LOCK_AND_BYTE;
    const size_t memindex=d&&d->memory_output?0U:d&&d->has_flag?1U:0U;
    const size_t bitindex=d&&d->memory_output&&d->has_flag?2U:
                         d&&d->memory_output?1U:d&&d->has_flag?2U:1U;
    const size_t flagindex=d&&d->has_flag?(d->memory_output?1U:0U):SIZE_MAX;
    MinicType mem,flag;
    size_t width,i;
    bool sign;
    if(!out||!program||!target||!fn||!frame||!ins||!info||!b||!d||
       !info->has_memory_clobber||info->register_clobber_count||
       count!=(d->has_flag?3U:2U)||(d->is_locked&&!info->is_volatile))
        return false;
    for(i=0U;i<count;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].early_clobber||b[i].has_fixed_register_binding||
           b[i].kind!=(i==memindex?
               (d->writes_memory?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
               i==flagindex?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if(!minic_type_pointee(fn->values[b[memindex].value].type,&mem)||
       !minic_x86_64_scalar_width(program,target,mem,&width,&sign)||
       width!=(byte_rmw?1U:8U)||
       !minic_x86_64_scalar_width(program,target,
           fn->values[b[bitindex].value].type,&width,&sign)||
       width!=(byte_rmw?4U:8U)||
       (d->has_flag&&
        (!minic_type_pointee(fn->values[b[flagindex].value].type,&flag)||
         !minic_type_equal(flag,minic_type_bool()))))
        return false;
    if(!x86_value_load(out,frame,b[memindex].value,"%r11")||
       !x86_value_load(out,frame,b[bitindex].value,"%r10"))
        return false;
    if(d->is_locked&&
       fputs("  .pushsection .smp_locks,\"a\"\n"
             "  .balign 4\n"
             "  .long 671f - .\n"
             "  .popsection\n"
             "671:\n",out)==EOF)
        return false;
    if(fprintf(out,"  %s%s %s, (%%r11)\n",
               d->is_locked?"lock; ":"",d->opcode,
               byte_rmw?"%r10b":"%r10")<0)
        return false;
    if(!d->has_flag)return true;
    return fputs("  setc %al\n",out)!=EOF&&
           x86_value_load(out,frame,b[flagindex].value,"%r10")&&
           fputs("  movb %al, (%r10)\n",out)!=EOF;
}









/* True CF carry-out propagation on 32-bit original Linux csum_add:
 * select GNU rm's register alternative. ALL inputs are materialized
 * before ADD, so ADC executes immediately with unmodified EFLAGS. */
static bool emit_add32_carry(FILE *out,const MinicC0Program *p,
    const MinicTargetInfo *t,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType ty;size_t bytes,i;bool sign;
    if(!out||!p||!t||!fn||!frame||!info||!ins||!b||
       !minic_x86_64_add32_carry_template(
           info->template_text,info->template_length)||
       info->is_goto||info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=3U)return false;
    for(i=0U;i<3U;++i){
        if(b[i].value>=fn->value_count||b[i].operand_index!=i||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i==0U?
             MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
             MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        ty=fn->values[b[i].value].type;
        if(i==0U&&!minic_type_pointee(ty,&ty))return false;
        if(!minic_x86_64_scalar_width(p,t,ty,&bytes,&sign)||
           bytes!=4U)return false;
    }
    return x86_value_load(out,frame,b[1].value,"%rax")&&
           x86_value_load(out,frame,b[2].value,"%r10")&&
           fputs("  addl %r10d, %eax\n"
                 "  adcl $0, %eax\n",out)!=EOF&&
           x86_value_load(out,frame,b[0].value,"%r11")&&
           fputs("  movl %eax, (%r11)\n",out)!=EOF;
}

/* Original Linux csum_tcpudp_nofold() x86 carry chain. The output is tied
 * to operand four, but operands one through three are g (register/memory)
 * and may each require arbitrary frame loads. Materialize every operand
 * before ADD: no flag-mutating calculation can intervene between ADCs. */
static bool emit_linux_tcpudp_nofold(
    FILE *out, const MinicC0Program *p, const MinicTargetInfo *t,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t bytes, i;
    bool sign;
    if (!out || !p || !t || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_tcpudp_nofold_template(
            info->template_text, info->template_length) ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 5U) return false;
    for (i = 0U; i < 5U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
        type = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(type, &type)) return false;
        if (!minic_x86_64_scalar_width(p, t, type, &bytes, &sign) ||
            bytes != 4U) return false;
    }
    return x86_value_load(out, frame, b[4].value, "%rax") &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           x86_value_load(out, frame, b[2].value, "%r11") &&
           x86_value_load(out, frame, b[3].value, "%r8") &&
           fputs("  addl %r10d, %eax\n"
                 "  adcl %r11d, %eax\n"
                 "  adcl %r8d, %eax\n"
                 "  adcl $0, %eax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

/* The GS segment base is a real x86 hardware register. Address-backed
 * Core lvalues carry the per-CPU symbol/field displacement, not ordinary
 * C values. Use caller-saved RAX/R10/R11, preserve x86 width and flags. */
static bool emit_linux_percpu_gs(FILE *out,const MinicC0Program *p,
    const MinicTargetInfo *t,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    unsigned int op;size_t bytes,i;bool sign;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType type;
    static const char *const mnemonics[]={
       "mov","mov","inc","dec","add","sub","and","or","xor"};
    char suffix;
    const char *reg;
    size_t checked;
    if(!out||!p||!t||!fn||!frame||!ins||!info||!b||
       !minic_x86_64_linux_percpu_form(info->template_text,
           info->template_length,&op,&bytes)||
       info->is_goto||info->has_memory_clobber!=(op!=0U)||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=
          ((op==2U||op==3U)?1U:2U))return false;
    for(i=0U;i<ins->value.structured_inline_asm.operand_count;++i){
        const bool memory=(i==0U&&op!=0U)||(i==1U&&op==0U);
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i==0U?
              (op==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE):
              (op==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)))
            return false;
        type=fn->values[b[i].value].type;
        if(memory||(i==0U&&op==0U)){
            if(!minic_type_pointee(type,&type))return false;
        }
        if (bytes == 8U && minic_type_is_pointer(type) &&
            ((op == 0U && i == 1U) || (op == 1U && i == 0U)))
            checked = 8U;
        else if(!minic_x86_64_scalar_width(p,t,type,&checked,&sign))
            return false;
        if (checked != bytes)
            return false;
    }
    suffix=bytes==1U?'b':bytes==2U?'w':bytes==4U?'l':'q';
    reg=bytes==1U?"%al":bytes==2U?"%ax":
         bytes==4U?"%eax":"%rax";
    if(op==0U){
        return x86_value_load(out,frame,b[1].value,"%r10")&&
               fprintf(out,"  mov%c %%gs:(%%r10), %s\n",suffix,reg)>=0&&
               x86_value_load(out,frame,b[0].value,"%r11")&&
               fprintf(out,"  mov%c %s, (%%r11)\n",suffix,reg)>=0;
    }
    if(op!=2U&&op!=3U&&
       !x86_value_load(out,frame,b[1].value,"%rax"))return false;
    if(!x86_value_load(out,frame,b[0].value,"%r10"))return false;
    return (op==2U||op==3U)?
       fprintf(out,"  %s%c %%gs:(%%r10)\n",mnemonics[op],suffix)>=0:
       fprintf(out,"  %s%c %s, %%gs:(%%r10)\n",
               mnemonics[op],suffix,reg)>=0;
}

/* Same exact ISA descriptor used by x86 input admission and physical
 * emission. Read the per-CPU GS displacement, combine it with live tied
 * input, and write the result into the canonical Core output lvalue. */
static bool emit_linux_gs_tied_alu(
    FILE *out, const MinicC0Program *p, const MinicTargetInfo *t,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const mnemonics[]={"add","sub","and","or","xor"};
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType v;
    size_t form_width, width = 0U, checked;
    unsigned int form;
    bool sign;
    if(!out||!p||!t||!fn||!frame||!ins||!info||!b||
       info->has_memory_clobber||info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    form=minic_x86_64_gs_tied_alu_form(
        info->template_text,info->template_length,&form_width);
    if (form==0U||form>5U) return false;
    for(size_t i=0U;i<3U;++i) {
        const MinicCoreStructuredInlineAsmOperand *vref=&b[i];
        if(vref->operand_index!=i||vref->value>=fn->value_count||
           vref->has_fixed_register_binding||vref->early_clobber||
           vref->kind!=(i==0U?
               MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
               i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        v=fn->values[vref->value].type;
        if(i!=2U && !minic_type_pointee(v,&v)) return false;
        if(i==2U && minic_type_is_pointer(v)) checked=8U;
        else if(!minic_x86_64_scalar_width(p,t,v,&checked,&sign))
            return false;
        if(i==0U)width=checked;
        if(checked!=width|| (checked!=4U&&checked!=8U))
            return false;
    }
    if(form_width!=0U&&form_width!=width) return false;
    if(!x86_value_load(out,frame,b[2].value,"%rax") ||
       !x86_value_load(out,frame,b[1].value,"%r10"))
        return false;
    if(fprintf(out,"  %s%c %%gs:(%%r10), %s\n",
         mnemonics[form-1U],width==4U?'l':'q',
         width==4U?"%eax":"%rax")<0)
        return false;
    return x86_value_load(out,frame,b[0].value,"%r11") &&
           fputs(width==4U?"  movl %eax, (%r11)\n":
                            "  movq %rax, (%r11)\n",out)!=EOF;
}

/* Exact Linux GNU PREFETCHW CPU ALTERNATIVE: retain patch records in ELF.
 * The one real memory lvalue is held in Core SSA and materialized in r10. */
static bool emit_linux_prefetchw_alt(
    FILE *out,const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *constant;
    if(!out||!fn||!frame||!ins||!info||!b||info->is_goto||
       info->has_memory_clobber||info->register_clobber_count||
       !minic_x86_64_linux_prefetchw_alt_form(
           info->template_text,info->template_length)||
       ins->value.structured_inline_asm.operand_count!=2U||
       b[0].operand_index!=0U||
       b[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       b[0].value>=fn->value_count||
       b[1].operand_index!=1U||
       b[1].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT||
       b[1].value>=fn->value_count||
       fn->values[b[0].value].definition>=fn->instruction_count||
       !minic_type_is_pointer(fn->values[b[1].value].type))
        return false;
    constant=&fn->instructions[fn->values[b[0].value].definition];
    if(constant->kind!=MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT||
       constant->value.integer_value!=0)
        return false;
    if(!x86_value_load(out,frame,b[1].value,"%r10"))
        return false;
    return fputs(
       "661:\n"
       "  prefetcht0 (%r10)\n"
       "662:\n"
       "  .skip -(((6651f-6641f)-(662b-661b))>0)*"
               "((6651f-6641f)-(662b-661b)),0x90\n"
       "663:\n"
       "  .pushsection .altinstructions,\"a\"\n"
       "  .long 661b - .\n"
       "  .long 6641f - .\n"
       "  .4byte (6*32+8)\n"
       "  .byte 663b-661b\n"
       "  .byte 6651f-6641f\n"
       "  .popsection\n"
       "  .pushsection .altinstr_replacement,\"ax\"\n"
       "6641:\n"
       "  prefetchw (%r10)\n"
       "6651:\n"
       "  .popsection\n",out)!=EOF;
}

/* Original APIC and Zen alternatives are two distinct hardware effects,
 * but share the GNU ELF alternative patch-record layout. The MOV fallback
 * and atomic XCHG replacement must use exactly the same MMIO address and
 * register; AMD's DIV replacement must keep RAX:RDX input zero with a true
 * one divisor, rather than executing DIV unconditionally. */
static bool emit_linux_apic_amd_alt(
    FILE *out,const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const unsigned int form=info?minic_x86_64_linux_apic_amd_alt_form(
        info->template_text,info->template_length):0U;
    const size_t count=form==1U?5U:3U;
    size_t i;
    if(!out||!fn||!frame||!ins||!info||!b||!form||info->is_goto||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=count)
        return false;
    for(i=0U;i<count;++i){
        const MinicCoreStructuredInlineAsmOperandKind kind=form==2U?
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            i==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
            i==4U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
                   MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if(b[i].operand_index!=i||b[i].kind!=kind||
           b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding)return false;
        if((form==1U&&(i==0U||i==1U||i==4U))&&
           !minic_type_is_pointer(fn->values[b[i].value].type))return false;
        if((form==2U||i==2U||i==3U)&&
           !minic_type_is_integer(fn->values[b[i].value].type))return false;
    }
    if(form==1U){
        if(!x86_value_load(out,frame,b[1].value,"%r10")||
           !x86_value_load(out,frame,b[3].value,"%r11")||
           !x86_value_load(out,frame,b[4].value,"%r9"))return false;
    }else{
        if(!x86_value_load(out,frame,b[0].value,"%rax")||
           !x86_value_load(out,frame,b[1].value,"%rdx")||
           !x86_value_load(out,frame,b[2].value,"%r10"))return false;
    }
    if(fputs("661:\n",out)==EOF)return false;
    if(form==1U && fputs("  movl %r11d, (%r10)\n",out)==EOF)
        return false;
    if(fputs("662:\n"
             "  .skip -(((6651f-6641f)-(662b-661b))>0)*"
               "((6651f-6641f)-(662b-661b)),0x90\n"
             "663:\n"
             "  .pushsection .altinstructions,\"a\"\n"
             "  .long 661b - .\n"
             "  .long 6641f - .\n",out)==EOF||
       fprintf(out,"  .4byte %s\n",
           form==1U?"(22*32+(5))":"(22*32+(1*32+1))")<0||
       fputs("  .byte 663b-661b\n"
             "  .byte 6651f-6641f\n"
             "  .popsection\n"
             "  .pushsection .altinstr_replacement,\"ax\"\n"
             "6641:\n",out)==EOF)return false;
    if(fputs(form==1U?"  xchgl %r11d, (%r10)\n":
                      "  divq %r10\n",out)==EOF||
       fputs("6651:\n  .popsection\n",out)==EOF)return false;
    if(form==1U){
        if(!x86_value_load(out,frame,b[0].value,"%r8")||
           fputs("  movl %r11d, (%r8)\n",out)==EOF)return false;
    }
    return true;
}

/* Original Linux ip_fast_csum: preserve carry propagation and early
 * short-header branch; tied input operands are real Core scalar values.
 * No arbitrary assembly substitution or source-function special case. */
static bool emit_ip_fast_csum(FILE *out, const MinicC0Program *p,
    const MinicTargetInfo *t, const MinicCoreFunction *fn,
    const MinicX86Frame *frame, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType ty; size_t i,width; bool sign;
    if (!out || !p || !t || !fn || !frame || !ins || !info || !b ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        !minic_x86_64_ip_fast_csum_template(info->template_text,
            info->template_length) ||
        ins->value.structured_inline_asm.operand_count!=5U) return false;
    for(i=0U;i<5U;++i) {
        if (b[i].operand_index!=i || b[i].value>=fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind!=(i<3U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))return false;
        ty=fn->values[b[i].value].type;
        if (i<3U && !minic_type_pointee(ty,&ty))return false;
        if(!minic_x86_64_scalar_width(p,t,ty,&width,&sign) ||
           width!=(i==1U || i==3U ? 8U : 4U))return false;
    }
    if(!x86_value_load(out,frame,b[3].value,"%r10") ||
       !x86_value_load(out,frame,b[4].value,"%r11"))return false;
    if(fputs("  movl (%r10), %r8d\n"
             "  subl $4, %r11d\n"
             "  jbe 2f\n"
             "  addl 4(%r10), %r8d\n"
             "  adcl 8(%r10), %r8d\n"
             "  adcl 12(%r10), %r8d\n"
             "1: adcl 16(%r10), %r8d\n"
             "  leaq 4(%r10), %r10\n"
             "  decl %r11d\n"
             "  jne 1b\n"
             "  adcl $0, %r8d\n"
             "  movl %r8d, %r11d\n"
             "  shrl $16, %r8d\n"
             "  addw %r11w, %r8w\n"
             "  adcl $0, %r8d\n"
             "  notl %r8d\n"
             "2:\n",out)==EOF)return false;
    if(!x86_value_load(out,frame,b[0].value,"%rax") ||
       fputs("  movl %r8d, (%rax)\n",out)==EOF ||
       !x86_value_load(out,frame,b[1].value,"%rax") ||
       fputs("  movq %r10, (%rax)\n",out)==EOF ||
       !x86_value_load(out,frame,b[2].value,"%rax") ||
       fputs("  movl %r11d, (%rax)\n",out)==EOF)return false;
    return true;
}

/* The third GNU input "0" initializes %eax, while ADD and ADC must be
 * adjacent because CF is live. Both operands are real Core scalars. */
static bool emit_checksum_fold(FILE *out,const MinicC0Program *p,
    const MinicTargetInfo *t,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType ty;size_t width,i;bool sign;
    if(!out||!p||!t||!fn||!frame||!ins||!info||!b||
       !minic_x86_64_checksum_fold_template(
            info->template_text,info->template_length)||
       /* Canonical Core marks every target-qualified ASM effect volatile,
        * including GNU source nonvolatile outputs. Source eligibility
        * remains exactly checked by x86_checksum_source. */
       info->is_goto||info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=3U)return false;
    for(i=0U;i<3U;++i){
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i==0U?
             MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
             MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))return false;
        ty=fn->values[b[i].value].type;
        if(i==0U&&!minic_type_pointee(ty,&ty))return false;
        if(!minic_x86_64_scalar_width(p,t,ty,&width,&sign)||
           width!=4U)return false;
    }
    return x86_value_load(out,frame,b[2].value,"%rax")&&
           x86_value_load(out,frame,b[1].value,"%r10")&&
           fputs("  addl %r10d, %eax\n"
                 "  adcl $0xffff, %eax\n",out)!=EOF&&
           x86_value_load(out,frame,b[0].value,"%r11")&&
           fputs("  movl %eax, (%r11)\n",out)!=EOF;
}

/* Linux's csum_partial uses ADDQ/ADCQ as a single carry chain.  Each GNU
 * memory "m" input is a genuine lowered Core address.  Load those addresses
 * with MOV, which does not change CF, before each dependent ADCQ; no C-level
 * arithmetic can replace the hardware carry propagation. */
static bool emit_linux_csum_partial(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_csum_partial_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *operand =
        ins->value.structured_inline_asm.operands;
    const size_t count = form == 1U || form == 6U ? 2U : form == 2U ? 6U : 3U;
    size_t i;
    MinicType type;
    size_t width;
    bool sign;
    if (out == NULL || program == NULL || target == NULL || fn == NULL ||
        frame == NULL || ins == NULL || info == NULL || operand == NULL ||
        form == 0U || info->is_goto || info->register_clobber_count ||
        info->has_memory_clobber != (form != 1U && form != 6U) ||
        ins->value.structured_inline_asm.operand_count != count) return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &operand[i];
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
            : form == 1U || form >= 3U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        if (op->operand_index != i || op->kind != kind ||
            op->value >= fn->value_count || op->early_clobber ||
            op->has_fixed_register_binding) return false;
        type = fn->values[op->value].type;
        if (i == 0U || kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
            if (!minic_type_pointee(type, &type)) return false;
        }
        if (form >= 3U && i == 2U) {
            size_t alignment;
            if (!minic_type_is_pointer(type) ||
                !minic_type_pointee(type, &type)) return false;
            if (minic_type_is_array(type)) {
                if (!minic_data_layout_type(target->data_layout, program, type,
                                            &width, &alignment) ||
                    width != (form == 3U ? 32U : form == 4U ? 16U : 8U))
                    return false;
            } else if (!minic_type_unqualified(type, &type) ||
                       !minic_type_is_char_integer(type)) return false;
        } else if (kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT &&
                   form >= 3U && form != 6U) {
            if (!minic_type_is_pointer(type)) return false;
        } else if (!minic_x86_64_scalar_width(
                       program, target, type, &width, &sign) || width != 8U)
            return false;
    }
    if (!x86_value_load(out, frame, operand[0].value, "%r11") ||
        fputs("  movq (%r11), %rax\n", out) == EOF) return false;
    if (form == 1U || form == 6U) {
        if (!x86_value_load(out, frame, operand[1].value, "%r10") ||
            fputs("  addq %r10, %rax\n", out) == EOF) return false;
    } else if (form == 2U) {
        for (i = 1U; i < count; ++i) {
            if (!x86_value_load(out, frame, operand[i].value, "%r10") ||
                fprintf(out, "  %sq (%%r10), %%rax\n", i == 1U ? "add" : "adc") < 0)
                return false;
        }
    } else {
        /* The third GNU "m" operand names the full byte region; the
         * instruction template itself addresses the base scalar pointer. */
        const size_t qwords = form == 3U ? 4U : form == 4U ? 2U : 1U;
        if (!x86_value_load(out, frame, operand[1].value, "%r10")) return false;
        for (i = 0U; i < qwords; ++i) {
            if (fprintf(out, "  %sq %zu(%%r10), %%rax\n",
                        i == 0U ? "add" : "adc", 8U * i) < 0) return false;
        }
    }
    return fputs("  adcq $0, %rax\n", out) != EOF &&
           x86_value_load(out, frame, operand[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}

/* Privileged Linux rdmsr/wrmsr with 1:/2: exception fixups. Both GNU
 * relocation entries are real, with architecture-defined type 9 or 8.
 * No executing these privileged opcodes in unprivileged regression tests. */
static bool emit_linux_msr(FILE *out,const MinicC0Program *program,
    const MinicTargetInfo *target,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const unsigned int form=minic_x86_64_linux_msr_form(
        info->template_text,info->template_length);
    const MinicCoreStructuredInlineAsmOperand *ops=
        ins->value.structured_inline_asm.operands;
    const size_t outputs=form==1U?2U:form==3U?3U:form==4U?1U:0U;
    const size_t count=(form==3U||form==4U)?4U:3U;
    static const char *const input_registers[]={"%rcx","%rax","%rdx"};
    static const char *const output_registers32[]={"%eax","%edx"};
    static const char *const output_registers64[]={"%rax","%rdx"};
    MinicType ty;size_t width,i;bool sign;
    if(!out||!program||!target||!fn||!frame||!ins||!info||!ops||
       !form||!info->is_volatile||info->is_goto||
       info->register_clobber_count||
       info->has_memory_clobber!=(form==2U||form==4U)||
       ins->value.structured_inline_asm.operand_count!=count)return false;
    if(form==3U || form==4U) {
        static const char *const in_regs_read[]={"%rcx"};
        static const char *const in_regs_write[]={"%rcx","%rax","%rdx"};
        const char *const *regs=form==3U?in_regs_read:in_regs_write;
        if(!info->is_volatile || info->is_goto ||
           info->has_memory_clobber!=(form==4U) ||
           info->register_clobber_count ||
           ins->value.structured_inline_asm.operand_count!=4U)return false;
        for(i=0U;i<4U;++i){
            const bool output=i<outputs;
            if(ops[i].value>=fn->value_count || ops[i].operand_index!=i ||
               ops[i].has_fixed_register_binding || ops[i].early_clobber ||
               ops[i].kind!=(output?
                 MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                 MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))return false;
            ty=fn->values[ops[i].value].type;
            if(output && !minic_type_pointee(ty,&ty))return false;
            if(!minic_x86_64_scalar_width(program,target,ty,&width,&sign) ||
               width!=(form==3U && output && i!=0U?8U:4U))return false;
        }
        for(i=0U;i<4U-outputs;++i)
            if(!x86_value_load(out,frame,ops[outputs+i].value,regs[i]))
                return false;
        if(form==3U){
            /* Type 11 exception fixup patches R11; the physical register
             * number 11 belongs in the GNU __ex_table type word. */
            if(fputs("1: rdmsr\n  xorl %r11d, %r11d\n2:\n"
                     "  .pushsection \"__ex_table\",\"a\"\n"
                     "  .balign 4\n  .long (1b)-.\n  .long (2b)-.\n"
                     "  .long (11 + (11 << 8))\n  .popsection\n",out)==EOF)
                return false;
        } else if(fputs("1: wrmsr\n  xorl %eax, %eax\n2:\n"
                       "  .pushsection \"__ex_table\",\"a\"\n"
                       "  .balign 4\n  .long (1b)-.\n  .long (2b)-.\n"
                       "  .long 10\n  .popsection\n",out)==EOF)
            return false;
        for(i=0U;i<outputs;++i){
            const char *reg=form==4U?"%eax":i==0U?"%r11d":
                i==1U?"%rax":"%rdx";
            const char *op=form==3U&&i!=0U?"movq":"movl";
            if(!x86_value_load(out,frame,ops[i].value,"%r10")||
               fprintf(out,"  %s %s, (%%r10)\n",op,reg)<0)return false;
        }
        return true;
    }
    for(i=0U;i<count;++i){
        const bool output=i<outputs;
        if(ops[i].value>=fn->value_count||ops[i].operand_index!=i||
           ops[i].has_fixed_register_binding||ops[i].early_clobber||
           ops[i].kind!=(output?
               MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
               MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))return false;
        if(output){
            if(!minic_type_pointee(fn->values[ops[i].value].type,&ty)||
               !minic_x86_64_scalar_width(program,target,ty,&width,&sign)||
               (width!=4U&&width!=8U))return false;
        }else if(!minic_x86_64_scalar_width(
                 program,target,fn->values[ops[i].value].type,
                 &width,&sign)||width!=4U)return false;
    }
    for(i=0U;i<count-outputs;++i)
        if(!x86_value_load(out,frame,ops[outputs+i].value,
                           input_registers[form==1U?0U:i]))
            return false;
    if(fprintf(out,
       "1:\n  %s\n2:\n"
       "  .pushsection \"__ex_table\",\"a\"\n"
       "  .balign 4\n"
       "  .long (1b) - .\n"
       "  .long (2b) - .\n"
       "  .long %u\n"
       "  .popsection\n",
       form==1U?"rdmsr":"wrmsr",form==1U?9U:8U)<0)return false;
    for(i=0U;i<outputs;++i){
        if(!minic_type_pointee(fn->values[ops[i].value].type,&ty)||
           !minic_x86_64_scalar_width(program,target,ty,&width,&sign)||
           !x86_value_load(out,frame,ops[i].value,"%r11")||
           fprintf(out,"  %s %s, (%%r11)\n",
            width==4U?"movl":"movq",
            width==4U?output_registers32[i]:output_registers64[i])<0)
           return false;
    }
    return true;
}

/* Linux's software fallback is executable before kernel runtime patches the
 * original CALL with the POPCNT replacement. ELF ALT section stores the
 * correct feature 151 and the two relative relocation expressions. */
static bool emit_hweight_alternative(FILE *out,const MinicC0Program *p,
    const MinicTargetInfo *t,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const unsigned int w=minic_x86_64_hweight_alternative_form(
        info->template_text,info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType ty;size_t bytes;bool sign;
    if(!out||!p||!t||!fn||!frame||!ins||!info||!b||
       (w!=4U&&w!=8U)||info->is_goto||
       info->register_clobber_count||info->has_memory_clobber||
       ins->value.structured_inline_asm.operand_count!=2U||
       b[0].operand_index!=0U||b[1].operand_index!=1U||
       b[0].value>=fn->value_count||b[1].value>=fn->value_count||
       b[0].has_fixed_register_binding||b[1].has_fixed_register_binding||
       b[0].early_clobber||b[1].early_clobber||
       b[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT||
       b[1].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       !minic_type_pointee(fn->values[b[0].value].type,&ty)||
       !minic_x86_64_scalar_width(p,t,ty,&bytes,&sign)||bytes!=w||
       !minic_x86_64_scalar_width(p,t,fn->values[b[1].value].type,
          &bytes,&sign)||bytes!=w)return false;
    if(!x86_value_load(out,frame,b[1].value,"%rdi"))return false;
    if(fprintf(out,
        "661:\n"
        "  call __sw_hweight%s\n"
        "662:\n663:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .long 661b - .\n"
        "  .long 6641f - .\n"
        "  .4byte 151\n"
        "  .byte 663b - 661b\n"
        "  .byte 6651f - 6641f\n"
        "  .popsection\n"
        "  .pushsection .altinstr_replacement,\"ax\"\n"
        "6641:\n"
        "  popcnt%s %s, %s\n"
        "6651:\n"
        "  .popsection\n",
        w==4U?"32":"64",w==4U?"l":"q",
        w==4U?"%edi":"%rdi",w==4U?"%eax":"%rax")<0)return false;
    return x86_value_load(out,frame,b[0].value,"%r11")&&
           fprintf(out,"  %s %s, (%%r11)\n",
              w==4U?"movl":"movq",w==4U?"%eax":"%rax")>=0;
}

/* One target-owned EDX:EAX clock/PMC emitter. Read hardware first,
 * then store exact GNU typed output widths to address-backed Core slots. */
static bool emit_x86_counter(FILE *out,const MinicC0Program *p,
    const MinicTargetInfo *t,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info) {
    const unsigned int f=minic_x86_64_linux_counter_form(
        info->template_text,info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const size_t count=ins->value.structured_inline_asm.operand_count;
    static const char *const r32[]={"%eax","%edx"};
    static const char *const r64[]={"%rax","%rdx"};
    MinicType ty;size_t w,i;bool sign;
    if(!out||!p||!t||!fn||!frame||!info||!ins||!b||!f||
       !info->is_volatile||info->is_goto||
       info->has_memory_clobber||
       info->register_clobber_count!=((f==3U||f==4U)?1U:0U)||
       ((f==3U||f==4U)&&(!info->register_clobbers||
         !clobber_is(&info->register_clobbers[0],"ecx")))||
       count!=((f==2U||f==5U)?3U:2U))return false;
    for(i=0U;i<count;++i) {
        if(b[i].value>=fn->value_count||b[i].operand_index!=i||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i<2U?
             MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
             MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))return false;
        if(i<2U){
            if(!minic_type_pointee(fn->values[b[i].value].type,&ty)||
               !minic_x86_64_scalar_width(p,t,ty,&w,&sign)||
               (w!=4U&&w!=8U))return false;
        }else if(!minic_x86_64_scalar_width(
                 p,t,fn->values[b[i].value].type,&w,&sign)||w!=4U)return false;
    }
    if((f==2U||f==5U)&&
       !x86_value_load(out,frame,b[2].value,"%rcx"))return false;
    if (f==4U) {
        /* The source contract is an exact-byte template, whose Linux
         * .altinstructions metadata and original numeric labels must be
         * preserved in the GNU assembler stream. */
        if (fwrite(info->template_text,1U,info->template_length,out) !=
                info->template_length || fputc('\n',out)==EOF) return false;
    } else if (fputs(f==1U?"  rdtsc\n":f==2U?"  rdpmc\n":
                   f==5U?"  xgetbv\n":"  rdtscp\n",out)==EOF) return false;
    for(i=0U;i<2U;++i){
        if(!minic_type_pointee(fn->values[b[i].value].type,&ty)||
           !minic_x86_64_scalar_width(p,t,ty,&w,&sign)||
           !x86_value_load(out,frame,b[i].value,"%r11")||
           fprintf(out,"  %s %s, (%%r11)\n",
              w==4U?"movl":"movq",w==4U?r32[i]:r64[i])<0)return false;
    }
    return true;
}

/* XSETBV accepts only the exact kernel template and three read-only Core
 * operands. Loading EAX/EDX/ECX simultaneously preserves the architectural
 * 64-bit value split, index selector and privilege fault semantics. */
static bool emit_linux_xsetbv(FILE *out, const MinicC0Program *program,
                              const MinicTargetInfo *target,
                              const MinicCoreFunction *function,
                              const MinicX86Frame *frame,
                              const MinicCoreInstruction *instruction,
                              const MinicCoreInlineAsm *source) {
    static const char *const registers[] = {"%rax", "%rdx", "%rcx"};
    const MinicCoreStructuredInlineAsmOperand *operands;
    size_t count, i;
    if (!out || !program || !target || !function || !frame ||
        !instruction || !source || !source->template_text ||
        source->template_length != 6U ||
        memcmp(source->template_text, "xsetbv", 6U) != 0 ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->register_clobber_count != 0U)
        return false;
    count = instruction->value.structured_inline_asm.operand_count;
    operands = instruction->value.structured_inline_asm.operands;
    if (count != 3U || !operands) return false;
    for (i = 0U; i < count; ++i) {
        size_t width;
        bool sign;
        if (operands[i].operand_index != i ||
            operands[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            operands[i].has_fixed_register_binding || operands[i].early_clobber ||
            operands[i].value >= function->value_count ||
            !minic_x86_64_scalar_width(program, target,
                function->values[operands[i].value].type, &width, &sign) ||
            width != 4U) return false;
    }
    for (i = 0U; i < count; ++i)
        if (!x86_value_load(out, frame, operands[i].value, registers[i]))
            return false;
    return fputs("  xsetbv\n", out) != EOF;
}

static bool emit_linux_fxsave(FILE *out, const MinicC0Program *program,
                              const MinicTargetInfo *target,
                              const MinicCoreFunction *function,
                              const MinicX86Frame *frame,
                              const MinicCoreInstruction *instruction,
                              const MinicCoreInlineAsm *source) {
    const unsigned int form = source ? minic_x86_64_linux_fxsave_form(
        source->template_text, source->template_length) : 0U;
    const MinicCoreStructuredInlineAsmOperand *operand;
    MinicType pointee;
    size_t image_size, image_alignment;
    if (!form || !out || !program || !target || !function || !frame ||
        !instruction || !source || !source->is_volatile || source->is_goto ||
        source->has_memory_clobber != (form == 1U) ||
        source->register_clobber_count != 0U ||
        instruction->value.structured_inline_asm.operand_count != 1U)
        return false;
    operand = instruction->value.structured_inline_asm.operands;
    if (!operand || operand[0].operand_index != 0U ||
        operand[0].kind != (form == 1U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) ||
        operand[0].has_fixed_register_binding || operand[0].early_clobber ||
        operand[0].value >= function->value_count ||
        !minic_type_pointee(function->values[operand[0].value].type, &pointee) ||
        !minic_data_layout_type(minic_target_info_data_layout(target),
            program, pointee, &image_size, &image_alignment) ||
        image_size < 512U || image_alignment < 16U)
        return false;
    if (!x86_value_load(out, frame, operand[0].value, "%r11"))
        return false;
    return fputs(form == 1U ? "  fxsave (%r11)\n" :
                              "  fxsaveq (%r11)\n", out) != EOF;
}

static bool emit_linux_x87_probe(FILE *out,
                                 const MinicCoreFunction *function,
                                 const MinicX86Frame *frame,
                                 const MinicCoreInstruction *instruction,
                                 const MinicCoreInlineAsm *source) {
    const MinicCoreStructuredInlineAsmOperand *bindings;
    size_t i;
    if (!out || !function || !frame || !instruction || !source ||
        !minic_x86_64_linux_x87_probe_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || source->register_clobber_count != 0U ||
        instruction->value.structured_inline_asm.operand_count != 2U)
        return false;
    bindings = instruction->value.structured_inline_asm.operands;
    if (!bindings) return false;
    for (i = 0U; i < 2U; ++i) {
        MinicType type;
        if (bindings[i].operand_index != i ||
            bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
            bindings[i].has_fixed_register_binding || bindings[i].early_clobber ||
            bindings[i].value >= function->value_count ||
            !minic_type_pointee(function->values[bindings[i].value].type, &type) ||
            !minic_type_equal(type, minic_type_unsigned_short()))
            return false;
    }
    if (fputs("  fninit\n", out) == EOF) return false;
    for (i = 0U; i < 2U; ++i) {
        if (!x86_value_load(out, frame, bindings[i].value, "%r11") ||
            fputs(i == 0U ? "  fnstsw (%r11)\n" :
                            "  fnstcw (%r11)\n", out) == EOF)
            return false;
    }
    return true;
}

/* The only 6-way Linux port-I/O emitter: native privileged in/out.
 * No fake C memory effects and no arbitrary-template passthrough. */
static bool emit_linux_port_io(FILE *out,const MinicC0Program *program,
    const MinicTargetInfo *target,const MinicCoreFunction *fn,
    const MinicX86Frame *frame,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const MinicX86PortIoDesc *d=minic_x86_64_port_io_description(
        minic_x86_64_port_io_form(info->template_text,info->template_length));
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    MinicType dt;size_t width;bool sign;
    if(!out||!program||!target||!fn||!frame||!ins||!info||!b||!d||
       !info->is_volatile||info->is_goto||
       info->has_memory_clobber||info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=2U||
       b[0].operand_index!=0U||b[1].operand_index!=1U||
       b[0].value>=fn->value_count||b[1].value>=fn->value_count||
       b[0].has_fixed_register_binding||b[1].has_fixed_register_binding||
       b[0].early_clobber||b[1].early_clobber||
       b[0].kind!=(d->is_input?
           MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
           MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)||
       b[1].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       !minic_x86_64_scalar_width(program,target,fn->values[b[1].value].type,
           &width,&sign)||width!=2U)return false;
    if(d->is_input){
        if(!minic_type_pointee(fn->values[b[0].value].type,&dt)||
           !minic_x86_64_scalar_width(program,target,dt,&width,&sign)||
           width!=d->width||
           !x86_value_load(out,frame,b[1].value,"%rdx")||
           fprintf(out,"  %s %%dx, %s\n",d->opcode,
             d->width==1U?"%al":d->width==2U?"%ax":"%eax")<0||
           !x86_value_load(out,frame,b[0].value,"%r11"))return false;
        return fprintf(out,"  mov%s %s, (%%r11)\n",
            d->width==1U?"b":d->width==2U?"w":"l",
            d->width==1U?"%al":d->width==2U?"%ax":"%eax")>=0;
    }
    if(!minic_x86_64_scalar_width(program,target,fn->values[b[0].value].type,
           &width,&sign)||width!=d->width||
       !x86_value_load(out,frame,b[0].value,"%rax")||
       !x86_value_load(out,frame,b[1].value,"%rdx"))return false;
    return fprintf(out,"  %s %s, %%dx\n",d->opcode,
        d->width==1U?"%al":d->width==2U?"%ax":"%eax")>=0;
}

/* Linux local_add_return +r,+m XADD machine hardware. The source
 * register is initialized from the C lvalue and written back with the old
 * memory value. The memory gets the sum: neither operand may be dropped. */
static bool emit_linux_local_xadd(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const size_t smp_width = minic_x86_64_linux_smp_xadd_width(
        info->template_text,info->template_length);
    const size_t percpu_width = minic_x86_64_linux_percpu_xadd_width(
        info->template_text, info->template_length);
    const size_t width = smp_width ? smp_width : percpu_width ? percpu_width :
        minic_x86_64_linux_local_xadd_width(info->template_text, info->template_length);
    static const char *const names[] = {"b","w","l","q"};
    const char *suffix, *source_reg;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t size, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !b || !width ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber ||
        info->register_clobber_count!=(smp_width?1U:0U) ||
        (smp_width && (!info->register_clobbers ||
         !clobber_is(&info->register_clobbers[0],"cc"))) ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    for (i = 0U; i < 2U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
            !minic_type_pointee(fn->values[b[i].value].type, &type) ||
            !minic_x86_64_scalar_width(program, target, type, &size, &sign) ||
            size != width || !minic_type_is_integer(type))
            return false;
    }
    /* r8 keeps the register C-lvalue address and r11 the memory address.
     * r10 receives the original register value, then XADD writes OLD memory
     * to r10 and NEW sum to memory; write the OLD value to the C register
     * operand before the surrounding C expression uses it. */
    if(width!=1U && width!=2U && width!=4U && width!=8U)return false;
    suffix=names[width==1U?0U:width==2U?1U:width==4U?2U:3U];
    source_reg=width==1U?"%r10b":width==2U?"%r10w":
        width==4U?"%r10d":"%r10";
    if(!x86_value_load(out,frame,b[0].value,"%r8") ||
       fprintf(out,"  mov%s (%%r8), %s\n",suffix,source_reg)<0 ||
       !x86_value_load(out,frame,b[1].value,"%r11"))return false;
    if(smp_width && fputs("  .pushsection .smp_locks,\"a\"\n"
        "  .balign 4\n  .long 671f - .\n  .popsection\n671:\n",out)==EOF)
        return false;
    if(fprintf(out,"  %sxadd%s %s, %s(%%r11)\n",
          smp_width?"lock; ":"",suffix,source_reg,
          percpu_width?"%gs:":"")<0)return false;
    return fprintf(out,"  mov%s %s, (%%r8)\n",suffix,source_reg)>=0;
}

/* Original __this_cpu_cmpxchg, not the lock-prefixed SMP CAS. GNU's +a
 * starts with the expected value and must receive the old memory value;
 * the +m operand names GS-relative per-CPU memory, not ordinary memory. */
static bool emit_linux_gs_cmpxchg(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *bind =
        ins->value.structured_inline_asm.operands;
    const size_t width = minic_x86_64_linux_gs_cmpxchg_width(
        info->template_text, info->template_length);
    const bool loop = minic_x86_64_linux_gs_cmpxchg_loop(
        info->template_text, info->template_length);
    const char *suffix, *new_register, *old_register;
    size_t i, actual;
    bool sign;
    MinicType type;
    if (!out || !program || !target || !fn || !frame || !bind ||
        !width || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (bind[i].operand_index != i || bind[i].value >= fn->value_count ||
            bind[i].has_fixed_register_binding ||
            bind[i].kind != (i == 0U
                ? (loop ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)
                : i == 1U
                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
                    : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[bind[i].value].type;
        if (i != 2U && !minic_type_pointee(type, &type)) return false;
        if (width == 8U && i == 1U && minic_type_is_pointer(type))
            actual = 8U;
        else if (!minic_type_is_integer(type) ||
                 !minic_x86_64_scalar_width(program, target, type, &actual, &sign))
            return false;
        if (actual != width)
            return false;
    }
    suffix = width == 1U ? "b" : width == 2U ? "w" :
             width == 4U ? "l" : "q";
    new_register = width == 1U ? "%r10b" : width == 2U ? "%r10w" :
                   width == 4U ? "%r10d" : "%r10";
    old_register = width == 1U ? "%al" : width == 2U ? "%ax" :
                   width == 4U ? "%eax" : "%rax";
    if (!x86_value_load(out, frame, bind[0].value, "%r8") ||
        !x86_value_load(out, frame, bind[1].value, "%r11") ||
        !x86_value_load(out, frame, bind[2].value, "%r10") ||
        fprintf(out, loop ? "  mov%s %%gs:(%%r11), %s\n"
                          : "  mov%s (%%r8), %s\n",
                suffix, old_register) < 0 ||
        (loop && fputs("1:\n", out) == EOF) ||
        fprintf(out, "  cmpxchg%s %s, %%gs:(%%r11)\n",
                suffix, new_register) < 0 ||
        (loop && fputs("  jnz 1b\n", out) == EOF))
        return false;
    return fprintf(out, "  mov%s %s, (%%r8)\n", suffix, old_register) >= 0;
}

static bool emit_linux_rep_stos(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *bind =
        ins->value.structured_inline_asm.operands;
    const size_t width = minic_x86_64_linux_rep_stos_width(
        info->template_text, info->template_length);
    const char *opcode = width == 2U ? "rep stosw" :
                         width == 4U ? "rep stosl" : "rep stosq";
    MinicType type;
    size_t checked, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !bind || !width ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 5U)
        return false;
    for (i = 0U; i < 5U; ++i) {
        if (bind[i].operand_index != i || bind[i].value >= fn->value_count ||
            bind[i].has_fixed_register_binding ||
            bind[i].kind != (i < 2U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[bind[i].value].type;
        if (i < 2U && !minic_type_pointee(type, &type)) return false;
        if (i == 3U) {
            if (!minic_type_is_pointer(type)) return false;
            checked = 8U;
        } else if (!minic_type_is_integer(type) ||
                   !minic_x86_64_scalar_width(program, target, type, &checked, &sign))
            return false;
        if (checked != (i == 2U ? width : 8U)) return false;
    }
    if (!x86_value_load(out, frame, bind[2].value, "%rax") ||
        !x86_value_load(out, frame, bind[3].value, "%rdi") ||
        !x86_value_load(out, frame, bind[4].value, "%rcx") ||
        fprintf(out, "  %s\n", opcode) < 0 ||
        !x86_value_load(out, frame, bind[0].value, "%r8") ||
        fputs("  movq %rcx, (%r8)\n", out) == EOF ||
        !x86_value_load(out, frame, bind[1].value, "%r8") ||
        fputs("  movq %rdi, (%r8)\n", out) == EOF)
        return false;
    return true;
}

static bool emit_linux_lowlevel_scalar(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_lowlevel_scalar_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t width;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !form ||
        !info->is_volatile || info->is_goto || info->register_clobber_count)
        return false;
    if (form == 1U)
        return ins->value.structured_inline_asm.operand_count == 0U &&
               fputs("  hlt\n", out) != EOF;
    if (form == 2U || form == 4U) {
        if (ins->value.structured_inline_asm.operand_count != 1U ||
            b[0].operand_index != 0U ||
            b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[0].value >= fn->value_count ||
            b[0].has_fixed_register_binding ||
            !minic_x86_64_scalar_width(program, target,
                fn->values[b[0].value].type, &width, &sign) ||
            (width != 2U && width != 4U && width != 8U)) return false;
        return x86_value_load(out, frame, b[0].value, "%rax") &&
               fputs(form == 2U ? "  lldt %ax\n" :
                                 "  movw %ax, %fs\n", out) != EOF;
    }
    if (form == 3U) {
        if (ins->value.structured_inline_asm.operand_count != 1U ||
            b[0].operand_index != 0U ||
            b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            b[0].value >= fn->value_count ||
            !minic_type_pointee(fn->values[b[0].value].type, &type) ||
            !minic_x86_64_scalar_width(program, target, type, &width, &sign) ||
            width != 2U) return false;
        return x86_value_load(out, frame, b[0].value, "%r11") &&
               fputs("  sldt (%r11)\n", out) != EOF;
    }
    if (ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding)
        return false;
    if (form == 5U) {
        if (b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            !minic_type_pointee(fn->values[b[0].value].type, &type) ||
            !minic_type_is_record(type) ||
            !minic_target_info_sizeof_type(target, program, type, &width) ||
            width != 16U ||
            !minic_x86_64_scalar_width(program, target,
                fn->values[b[1].value].type, &width, &sign) || width != 8U)
            return false;
        return x86_value_load(out, frame, b[0].value, "%r11") &&
               x86_value_load(out, frame, b[1].value, "%rax") &&
               fputs("  invpcid (%r11), %rax\n", out) != EOF;
    }
    if (form == 6U) {
        if (b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
            b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            !minic_type_pointee(fn->values[b[0].value].type, &type) ||
            !minic_x86_64_scalar_width(program, target, type, &width, &sign) ||
            width != 8U) return false;
        return x86_value_load(out, frame, b[0].value, "%r11") &&
               x86_value_load(out, frame, b[1].value, "%rax") &&
               fputs("  btcq %rax, (%r11)\n", out) != EOF;
    }
    if (form == 7U || form == 8U) {
        const size_t expected = form == 7U ? 4U : 8U;
        if (b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            !minic_type_pointee(fn->values[b[0].value].type, &type) ||
            !minic_x86_64_scalar_width(program, target, type, &width, &sign) ||
            width != expected ||
            !minic_x86_64_scalar_width(program, target,
                fn->values[b[1].value].type, &width, &sign) ||
            width != expected) return false;
        return x86_value_load(out, frame, b[0].value, "%r11") &&
               x86_value_load(out, frame, b[1].value, "%rax") &&
               fputs(form == 7U ? "  movntil %eax, (%r11)\n" :
                                  "  movntiq %rax, (%r11)\n", out) != EOF;
    }
    return false;
}

static bool emit_linux_boot_flags(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_boot_flags_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i, width, expected;
    bool sign;
    MinicType type;
    if (!out || !program || !target || !fn || !frame || !form ||
        !info->is_volatile || info->is_goto || info->register_clobber_count ||
        (form <= 2U && ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 3U : 2U)) ||
        (form == 3U && ins->value.structured_inline_asm.operand_count != 6U) ||
        (form == 4U && ins->value.structured_inline_asm.operand_count != 3U))
        return false;
    expected = ins->value.structured_inline_asm.operand_count;
    for (i = 0U; i < expected; ++i) {
        MinicCoreStructuredInlineAsmOperandKind required;
        if (form == 1U) required = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                       : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        else if (form == 2U) required = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        else required = i < (form == 3U ? 4U : 2U)
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].kind != required)
            return false;
        type = fn->values[b[i].value].type;
        if (required == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            required == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            required == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE)
            if (!minic_type_pointee(type, &type)) return false;
        if (form == 1U && i == 0U && minic_type_is_bool_integer(type))
            continue;
        if (!minic_type_is_integer(type) ||
            !minic_x86_64_scalar_width(program, target, type, &width, &sign))
            return false;
        if (width != (form <= 3U ? 4U : 8U) &&
            !(form <= 2U && i == expected - 1U && width == 8U))
            return false;
    }
    if (form == 1U) {
        return x86_value_load(out, frame, b[1].value, "%r11") &&
               x86_value_load(out, frame, b[2].value, "%rax") &&
               fputs("  btl %eax, (%r11)\n  setc %al\n  movzbq %al, %rax\n",out)!=EOF &&
               x86_value_load(out, frame, b[0].value, "%r10") &&
               fputs("  movb %al, (%r10)\n", out) != EOF;
    }
    if (form == 2U) {
        return x86_value_load(out, frame, b[0].value, "%r11") &&
               x86_value_load(out, frame, b[1].value, "%rax") &&
               fputs("  btsl %eax, (%r11)\n", out) != EOF;
    }
    if (form == 3U) {
        if (!x86_value_load(out, frame, b[4].value, "%rax") ||
            !x86_value_load(out, frame, b[5].value, "%rcx") ||
            fputs("  pushq %rbx\n  cpuid\n", out) == EOF)
            return false;
        for (i = 0U; i < 4U; ++i) {
            static const char *const regs[] = {"%eax","%ebx","%ecx","%edx"};
            if (!x86_value_load(out, frame, b[i].value, "%r11") ||
                fprintf(out, "  movl %s, (%%r11)\n", regs[i]) < 0)
                return false;
        }
        return fputs("  popq %rbx\n", out) != EOF;
    }
    if (form == 4U) {
        return x86_value_load(out, frame, b[2].value, "%rdx") &&
               fputs("  pushfq\n  pushfq\n  popq %rax\n"
                     "  movq %rax, %r10\n  xorq %rdx, %r10\n"
                     "  pushq %r10\n  popfq\n  pushfq\n"
                     "  popq %r10\n  popfq\n", out) != EOF &&
               x86_value_load(out, frame, b[0].value, "%r11") &&
               fputs("  movq %rax, (%r11)\n", out) != EOF &&
               x86_value_load(out, frame, b[1].value, "%r11") &&
               fputs("  movq %r10, (%r11)\n", out) != EOF;
    }
    return false;
}

static bool emit_linux_delay_mul(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType high_type, low_type;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_delay_mul_form(
            info->template_text, info->template_length) ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    for (i = 0U; i < 4U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding ||
            b[i].kind != (i < 2U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            (b[i].early_clobber && i != 1U)) return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &high_type) ||
        !minic_type_pointee(fn->values[b[1].value].type, &low_type) ||
        !minic_type_equal(high_type, minic_type_unsigned_long()) ||
        !minic_type_equal(low_type, minic_type_int()) ||
        !minic_type_equal(fn->values[b[2].value].type,
                          minic_type_unsigned_long()) ||
        !minic_type_equal(fn->values[b[3].value].type,
                          minic_type_unsigned_long())) return false;
    return x86_value_load(out, frame, b[2].value, "%rax") &&
           x86_value_load(out, frame, b[3].value, "%rdx") &&
           fputs("  mull %edx\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rdx, (%r11)\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

static bool emit_linux_delay_wait(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = minic_x86_64_linux_delay_wait_form(
        info->template_text, info->template_length);
    MinicType lvalue_type;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b || !form ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 1U : 3U)) return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (form == 1U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
    if (form == 1U) {
        if (!minic_type_pointee(fn->values[b[0].value].type, &lvalue_type) ||
            !minic_type_equal(lvalue_type, minic_type_unsigned_long()))
            return false;
        return x86_value_load(out, frame, b[0].value, "%r11") &&
               fputs("  movq (%r11), %rax\n"
                     "  testq %rax, %rax\n  jz 883f\n  jmp 881f\n"
                     "  .p2align 4\n881: jmp 882f\n  .p2align 4\n"
                     "882: decq %rax\n  jnz 882b\n883: decq %rax\n",out)!=EOF &&
               x86_value_load(out, frame, b[0].value, "%r11") &&
               fputs("  movq %rax, (%r11)\n", out) != EOF;
    }
    for (i = 0U; i < 3U; ++i)
        if (!minic_type_equal(fn->values[b[i].value].type,
                              minic_type_unsigned_int())) return false;
    return x86_value_load(out, frame, b[0].value, "%rcx") &&
           x86_value_load(out, frame, b[1].value, "%rdx") &&
           x86_value_load(out, frame, b[2].value, "%rax") &&
           fputs("  tpause %ecx\n", out) != EOF;
}

static bool emit_linux_identity_load(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_identity_load_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U ||
        !clobber_is(&info->register_clobbers[0], "r9") ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        b[0].operand_index || b[0].value >= fn->value_count ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].early_clobber || b[0].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long())) return false;
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq (%r11), %r9\n", out) != EOF;
}

/* IPv6 checksum: all five ADD/ADC carry edges are architecturally linked.
 * Do not split into C addition: it would lose the end-around carry when a
 * source 64-bit word overflows the running sum. */
static bool emit_linux_ipv6_checksum(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType sum_type;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_ipv6_checksum_form(
            info->template_text, info->template_length) ||
        info->is_goto || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    for (i = 0U; i < 4U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &sum_type) ||
        !minic_type_is_integer(sum_type) ||
        !minic_type_is_integer(fn->values[b[1].value].type) ||
        !minic_type_is_pointer(fn->values[b[2].value].type) ||
        !minic_type_is_pointer(fn->values[b[3].value].type)) return false;
    return x86_value_load(out, frame, b[1].value, "%rax") &&
           x86_value_load(out, frame, b[2].value, "%r10") &&
           x86_value_load(out, frame, b[3].value, "%r11") &&
           fputs("  addq (%r10), %rax\n"
                 "  adcq 8(%r10), %rax\n"
                 "  adcq (%r11), %rax\n"
                 "  adcq 8(%r11), %rax\n"
                 "  adcq $0, %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}

static bool emit_linux_lock_bitcc(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = minic_x86_64_linux_lock_bitcc_form(
        info->template_text, info->template_length);
    const size_t mi = form == 1U ? 0U : 1U;
    const size_t fi = 1U-mi;
    MinicType mem, flag;
    size_t width, input_width, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !ins || !info || !b ||
        !form || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    for (i = 0U; i < 3U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == mi
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
                : i == fi ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                          : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[mi].value].type, &mem) ||
        !minic_type_pointee(fn->values[b[fi].value].type, &flag) ||
        !minic_type_equal(flag, minic_type_bool()) ||
        !minic_x86_64_scalar_width(program,target,mem,&width,&sign) ||
        width != (form == 1U ? 4U : 1U) ||
        !minic_x86_64_scalar_width(program,target,
            fn->values[b[2].value].type,&input_width,&sign) ||
        (form == 1U ? input_width != 4U : input_width != 1U))
        return false;
    if (!x86_value_load(out,frame,b[2].value,"%r10") ||
        !x86_value_load(out,frame,b[mi].value,"%r11") ||
        fputs("  .pushsection .smp_locks,\"a\"\n"
              "  .balign 4\n  .long 671f - .\n  .popsection\n671:\n",
              out)==EOF) return false;
    if (form == 1U) {
        if (fputs("  lock; btsl %r10d, (%r11)\n  setc %al\n",out)==EOF)
            return false;
    } else if (fputs("  lock; andb %r10b, (%r11)\n  sets %al\n",out)==EOF)
        return false;
    return x86_value_load(out,frame,b[fi].value,"%r11") &&
           fputs("  movb %al, (%r11)\n",out)!=EOF;
}

static bool emit_linux_boot_string(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = minic_x86_64_linux_boot_string_form(
        info->template_text, info->template_length);
    const size_t outputs = form == 1U ? 4U : form == 2U ? 2U : 3U;
    const size_t inputs = form == 1U ? 0U : form == 2U ? 3U : 4U;
    MinicType type;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b || !form ||
        info->is_goto || info->register_clobber_count ||
        info->has_memory_clobber != (form != 2U) ||
        ins->value.structured_inline_asm.operand_count != outputs+inputs)
        return false;
    for (i = 0U; i < outputs+inputs; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected =
            form == 1U ? (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) :
            i < outputs ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].kind != expected ||
            (b[i].early_clobber && !(form == 3U && i < 3U)))
            return false;
        if (i < outputs &&
            !minic_type_pointee(fn->values[b[i].value].type,&type))
            return false;
        if (form == 1U) {
            if (!(i == 0U ? minic_type_equal(type,minic_type_bool()) :
                  i == 3U ? minic_type_equal(type,
                                              minic_type_unsigned_long()) :
                            minic_type_is_pointer(type))) return false;
        } else if (form == 2U) {
            if (!minic_type_equal(i < 2U ? type :
                                  fn->values[b[i].value].type,
                                  minic_type_unsigned_int())) return false;
        } else if (i < 3U ? !minic_type_equal(type,minic_type_long()) :
                   i <= 4U ? !minic_type_equal(
                        fn->values[b[i].value].type,
                        minic_type_unsigned_long()) :
                        !minic_type_is_pointer(fn->values[b[i].value].type))
            return false;
    }
    if (form == 1U) {
        if (!x86_value_load(out,frame,b[1].value,"%r11") ||
            fputs("  movq (%r11), %rdi\n",out)==EOF ||
            !x86_value_load(out,frame,b[2].value,"%r11") ||
            fputs("  movq (%r11), %rsi\n",out)==EOF ||
            !x86_value_load(out,frame,b[3].value,"%r11") ||
            fputs("  movq (%r11), %rcx\n"
                  "  repe; cmpsb\n  setne %al\n",out)==EOF ||
            !x86_value_load(out,frame,b[0].value,"%r11") ||
            fputs("  movb %al, (%r11)\n",out)==EOF ||
            !x86_value_load(out,frame,b[1].value,"%r11") ||
            fputs("  movq %rdi, (%r11)\n",out)==EOF ||
            !x86_value_load(out,frame,b[2].value,"%r11") ||
            fputs("  movq %rsi, (%r11)\n",out)==EOF ||
            !x86_value_load(out,frame,b[3].value,"%r11")) return false;
        return fputs("  movq %rcx, (%r11)\n",out)!=EOF;
    }
    if (form == 2U) {
        return x86_value_load(out,frame,b[2].value,"%r10") &&
               x86_value_load(out,frame,b[3].value,"%rax") &&
               x86_value_load(out,frame,b[4].value,"%rdx") &&
               fputs("  divl %r10d\n",out)!=EOF &&
               x86_value_load(out,frame,b[0].value,"%r11") &&
               fputs("  movl %eax, (%r11)\n",out)!=EOF &&
               x86_value_load(out,frame,b[1].value,"%r11") &&
               fputs("  movl %edx, (%r11)\n",out)!=EOF;
    }
    return x86_value_load(out,frame,b[5].value,"%rdi") &&
           x86_value_load(out,frame,b[6].value,"%rsi") &&
           x86_value_load(out,frame,b[3].value,"%rcx") &&
           x86_value_load(out,frame,b[4].value,"%r10") &&
           fputs("  rep; movsq\n  movq %r10, %rcx\n  rep; movsb\n",out)!=EOF &&
           x86_value_load(out,frame,b[0].value,"%r11") &&
           fputs("  movq %rcx, (%r11)\n",out)!=EOF &&
           x86_value_load(out,frame,b[1].value,"%r11") &&
           fputs("  movq %rdi, (%r11)\n",out)!=EOF &&
           x86_value_load(out,frame,b[2].value,"%r11") &&
           fputs("  movq %rsi, (%r11)\n",out)!=EOF;
}

static bool emit_linux_vdso_syscall(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    const bool clock_form = count == 5U;
    const size_t first_input = clock_form ? 2U : 1U;
    MinicType type;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_vdso_syscall_form(
            info->template_text, info->template_length) ||
        info->is_goto || !info->has_memory_clobber ||
        (count != 5U && count != 4U) ||
        info->register_clobber_count != (clock_form ? 2U : 0U) ||
        (clock_form &&
         (!clobber_is(&info->register_clobbers[0], "rcx") ||
          !clobber_is(&info->register_clobbers[1], "r11")))) return false;
    for (i = 0U; i < count; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : (clock_form && i == 1U)
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type,minic_type_long()) ||
        (clock_form &&
         !minic_type_is_pointer(fn->values[b[1].value].type)) ||
        !minic_type_is_integer(fn->values[b[first_input].value].type) ||
        (clock_form
            ? !minic_type_is_integer(fn->values[b[first_input+1U].value].type)
            : !minic_type_is_pointer(fn->values[b[first_input+1U].value].type)) ||
        !minic_type_is_pointer(fn->values[b[first_input+2U].value].type))
        return false;
    /* The =m timespec operand is an output memory dependency, not an extra
     * argument or a synthetic C record to write back.  RSI already passes
     * its address to the Linux system call. */
    return x86_value_load(out,frame,b[first_input+2U].value,"%rsi") &&
           x86_value_load(out,frame,b[first_input+1U].value,"%rdi") &&
           x86_value_load(out,frame,b[first_input].value,"%rax") &&
           fputs("  syscall\n",out)!=EOF &&
           x86_value_load(out,frame,b[0].value,"%r11") &&
           fputs("  movq %rax, (%r11)\n",out)!=EOF;
}

static bool emit_linux_vdso_cpunode(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType output_type;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !b ||
        !minic_x86_64_linux_vdso_cpunode_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    for (i=0U;i<3U;++i)
        if (b[i].operand_index!=i || b[i].value>=fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind!=(i==0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
    if (!minic_type_pointee(fn->values[b[0].value].type,&output_type) ||
        !minic_type_equal(output_type,minic_type_unsigned_int()) ||
        !minic_type_is_integer(fn->values[b[1].value].type) ||
        !minic_type_is_integer(fn->values[b[2].value].type)) return false;
    /* LSL is the original instruction. .altinstructions records feature
     * RDPID, while the exact replacement bytes target EAX.  The padding
     * expression ensures old/replacement lengths match Linux patch logic. */
    return x86_value_load(out,frame,b[2].value,"%r10") &&
           fputs("661:\n  lsl %r10d, %eax\n662:\n"
                 "  .skip -(((6651f-6641f)-(662b-661b))>0) * "
                 "((6651f-6641f)-(662b-661b)),0x90\n"
                 "663:\n  .pushsection .altinstructions,\"a\"\n"
                 "  .long 661b - .\n  .long 6641f - .\n"
                 "  .4byte (16*32+22)\n"
                 "  .byte 663b-661b\n  .byte 6651f-6641f\n"
                 "  .popsection\n"
                 "  .pushsection .altinstr_replacement,\"ax\"\n"
                 "6641:\n  .byte 0xf3,0x0f,0xc7,0xf8\n"
                 "6651:\n  .popsection\n",out)!=EOF &&
           x86_value_load(out,frame,b[0].value,"%r11") &&
           fputs("  movl %eax, (%r11)\n",out)!=EOF;
}

/* Target-owned atomic RMW hardware effect, including original 64-bit
 * GNU =m + extra m-input alias form and 32/64 unary INC/DEC.
 * The target admits complete typed C constraints; Core carries only generic
 * memory/readwrite, memory/input and scalar/input roles. */
static bool emit_linux_atomic_rmw(
    FILE *out,const MinicC0Program *program,const MinicTargetInfo *target,
    const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicX86AtomicRmwDesc *d=minic_x86_64_atomic_rmw_description(
        minic_x86_64_atomic_rmw_form(info->template_text,info->template_length));
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const size_t n=ins->value.structured_inline_asm.operand_count;
    const bool unary=d?d->unary:false;
    const bool legacy=d && n==1U+(unary?0U:1U)+1U;
    MinicType mt,dep;
    size_t bytes,i;
    bool sign;
    if(!out||!program||!target||!fn||!frame||!b||!d||!info||
       !info->is_volatile||info->is_goto||
       (d->locked && !info->has_memory_clobber)||
       info->register_clobber_count||
       n!=1U+(unary?0U:1U)+(legacy?1U:0U)||
       (legacy && (!d->locked||d->width!=8U||
          (!unary&&strcmp(d->opcode,"addq")!=0&&
                   strcmp(d->opcode,"subq")!=0))))
        return false;
    for(i=0U;i<n;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i==0U?
               MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
               (!unary&&i==1U)?
                   MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
                   MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT))
            return false;
    if(!minic_type_pointee(fn->values[b[0].value].type,&mt)||
       !minic_x86_64_scalar_width(program,target,mt,&bytes,&sign)||
       bytes!=d->width)return false;
    if(!unary &&
       (!minic_x86_64_scalar_width(program,target,
            fn->values[b[1].value].type,&bytes,&sign)||
        bytes!=d->width))
        return false;
    if(legacy &&
       (!minic_type_pointee(fn->values[b[n-1U].value].type,&dep)||
        !minic_x86_64_scalar_width(program,target,dep,&bytes,&sign)||
        bytes!=d->width))
        return false;
    if((!unary&&!x86_value_load(out,frame,b[1].value,"%r10"))||
       !x86_value_load(out,frame,b[0].value,"%r11"))
        return false;
    if(d->locked &&
       fputs("  .pushsection .smp_locks,\"a\"\n"
             "  .balign 4\n"
             "  .long 671f - .\n"
             "  .popsection\n"
             "671:\n",out)==EOF)
        return false;
    if(unary)return fprintf(out,"  %s%s (%%r11)\n",
          d->locked?"lock; ":"",d->opcode)>=0;
    return fprintf(out,"  %s%s %s, (%%r11)\n",
          d->locked?"lock; ":"",d->opcode,
          d->width==4U?"%r10d":"%r10")>=0;
}

/* Linux lock cmpxchg16b is a TRUE atomic 128-bit transaction. RDX:RAX
 * receives the prior 128-bit value, RBX:RCX provides the desired new value,
 * and ZF is the separately observable `=@cce` output. Keep the SMP lock
 * patch relocation and preserve ABI callee-saved RBX across the special
 * GNU operand contract, including when the C object itself is 16 bytes. */
static bool emit_linux_cmpxchg128(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = info ? minic_x86_64_linux_cmpxchg128_form(
        info->template_text, info->template_length) : 0U;
    const bool alt = form == 2U;
    const size_t at = alt ? 0U : 1U;
    const size_t count = alt ? 5U : 6U;
    size_t i, width;
    bool sign;
    MinicType pointee;
    if (!out || !program || !target || !fn || !frame || !info || !b || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != (alt ? 1U : 0U) ||
        ins->value.structured_inline_asm.operand_count != count)
        return false;
    for (i=0U;i<count;++i) {
        MinicCoreStructuredInlineAsmOperandKind kind =
            !alt && i==0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            i==at ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i==at+1U || i==at+2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                             MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != kind || b[i].early_clobber ||
            b[i].has_fixed_register_binding)
            return false;
        pointee = fn->values[b[i].value].type;
        if (i<at+3U && !minic_type_pointee(pointee, &pointee))
            return false;
        if (i==at) {
            if (!minic_type_is_integer(pointee) ||
                !minic_target_info_sizeof_type(target, program, pointee, &width) ||
                width!=16U)
                return false;
        } else if (!minic_x86_64_scalar_width(
                       program, target, pointee, &width, &sign) ||
                   width != (!alt && i==0U ? 1U : 8U))
            return false;
    }
    if (fputs("  subq $16, %rsp\n  movq %rbx, (%rsp)\n",out)==EOF ||
        !x86_value_load(out,frame,b[at+1U].value,"%r11") ||
        fputs("  movq (%r11), %rax\n",out)==EOF ||
        !x86_value_load(out,frame,b[at+2U].value,"%r11") ||
        fputs("  movq (%r11), %rdx\n",out)==EOF ||
        !x86_value_load(out,frame,b[at+3U].value,"%rbx") ||
        !x86_value_load(out,frame,b[at+4U].value,"%rcx") ||
        !x86_value_load(out,frame,b[at].value,"%r11"))
        return false;
    if (!alt && fputs("  .pushsection .smp_locks,\"a\"\n"
              "  .balign 4\n"
              "  .long 671f - .\n"
              "  .popsection\n"
              "671:\n"
              "  lock; cmpxchg16b (%r11)\n"
              "  sete %r10b\n",out)==EOF)
        return false;
    if (alt && fputs(
              "661:\n"
              "  leaq (%r11), %rsi\n"
              "  call this_cpu_cmpxchg16b_emu\n"
              "662:\n"
              "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                  "((6651f-6641f)-(662b-661b)),0x90\n"
              "663:\n"
              "  .pushsection .altinstructions,\"a\"\n"
              "  .long 661b - .\n"
              "  .long 6641f - .\n"
              "  .4byte (4*32+13)\n"
              "  .byte 663b-661b\n"
              "  .byte 6651f-6641f\n"
              "  .popsection\n"
              "  .pushsection .altinstr_replacement,\"ax\"\n"
              "6641:\n"
              "  cmpxchg16b %gs:(%r11)\n"
              "6651:\n"
              "  .popsection\n",out)==EOF)
        return false;
    if (!x86_value_load(out,frame,b[at+1U].value,"%r11") ||
        fputs("  movq %rax, (%r11)\n",out)==EOF ||
        !x86_value_load(out,frame,b[at+2U].value,"%r11") ||
        fputs("  movq %rdx, (%r11)\n",out)==EOF ||
        (!alt && (!x86_value_load(out,frame,b[0].value,"%r11") ||
                  fputs("  movb %r10b, (%r11)\n",out)==EOF)) ||
        fputs("  movq (%rsp), %rbx\n"
              "  addq $16, %rsp\n",out)==EOF)
        return false;
    return true;
}

/* Linux original arch_cmpxchg returns previous memory through RAX.
 * Unlike trycas it has NO CC output; the fourth Core operand is the
 * tied "0" scalar expected value and the first is a write-only C lvalue. */
static bool emit_linux_raw_cmpxchg(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicX86TryCasDesc *d = minic_x86_64_trycas_desc(
        minic_x86_64_raw_cmpxchg_form(info->template_text, info->template_length));
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i, width;
    bool sign;
    MinicType type;
    if (!out || !program || !target || !fn || !frame || !info || !b || !d ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    for (i = 0U; i < 4U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                           MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[b[i].value].type;
        if (i < 2U && !minic_type_pointee(type, &type)) return false;
        width = 0U;
        if (!minic_x86_64_scalar_width(program, target, type, &width, &sign) ||
            width != d->width ||
            !(minic_type_is_integer(type) || minic_type_is_pointer(type))) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MINIC_X86_RAW_CMPXCHG_REJECT function=%s operand=%zu "
                    "type_base=%d depth=%u width=%zu expected=%zu\n",
                    fn->name ? fn->name : "?", i, (int)type.base_kind,
                    type.pointer_depth, width, d->width);
            return false;
        }
    }
    if (!x86_value_load(out, frame, b[3].value, "%rax") ||
        !x86_value_load(out, frame, b[2].value, "%r10") ||
        !x86_value_load(out, frame, b[1].value, "%r11"))
        return false;
    if (d->locked &&
        fputs("  .pushsection .smp_locks,\"a\"\n"
              "  .balign 4\n"
              "  .long 671f - .\n"
              "  .popsection\n"
              "671:\n", out) == EOF)
        return false;
    if (fprintf(out, "  %s%s %s, (%%r11)\n",
                d->locked ? "lock; " : "", d->op, d->new_reg) < 0 ||
        !x86_value_load(out, frame, b[0].value, "%r11"))
        return false;
    return fprintf(out, "  %s\n", d->store_old) >= 0;
}

/* Genuine x86 CMPXCHG: use AL/AX/EAX/RAX comparator, return ZF,
 * update old on failure, and emit SMP patch metadata when locked. */
static bool emit_linux_trycas(
    FILE *out,const MinicC0Program *program,const MinicTargetInfo *target,
    const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const MinicX86TryCasDesc *d=minic_x86_64_trycas_desc(
        minic_x86_64_trycas_form(info->template_text,info->template_length));
    MinicType t;
    size_t i,width;
    bool sign;
    if(!out||!program||!target||!fn||!frame||!info||!b||!d||
       !info->is_volatile||info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=4U)
        return false;
    for(i=0U;i<4U;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].early_clobber||b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                        i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                        i==2U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
                              MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    for(i=0U;i<3U;++i)
        if(!minic_type_pointee(fn->values[b[i].value].type,&t)||
           !minic_x86_64_scalar_width(program,target,t,&width,&sign)||
           width!=(i==0U?1U:d->width))
            return false;
    if(!minic_x86_64_scalar_width(program,target,
          fn->values[b[3].value].type,&width,&sign)||width!=d->width)
        return false;
    if(!x86_value_load(out,frame,b[2].value,"%r11")||
       fprintf(out,"  %s\n",d->load_old)<0||
       !x86_value_load(out,frame,b[3].value,"%r10")||
       !x86_value_load(out,frame,b[1].value,"%r11"))
        return false;
    if(d->locked&&
       fputs("  .pushsection .smp_locks,\"a\"\n"
             "  .balign 4\n"
             "  .long 671f - .\n"
             "  .popsection\n"
             "671:\n",out)==EOF)
        return false;
    return fprintf(out,"  %s%s %s, (%%r11)\n",
             d->locked?"lock; ":"",d->op,d->new_reg)>=0&&
           fputs("  sete %r8b\n",out)!=EOF&&
           x86_value_load(out,frame,b[2].value,"%r11")&&
           fprintf(out,"  %s\n",d->store_old)>=0&&
           x86_value_load(out,frame,b[0].value,"%r11")&&
           fputs("  movb %r8b, (%r11)\n",out)!=EOF;
}

/* One physical x86 owner for Linux unary and binary locked RMWcc.
 * The original .smp_locks PREL32 entry, hardware locked write and final
 * ZF/SF SETcc all remain machine-verified. No generic Core ISA new case. */
static bool emit_linux_atomic_cc(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicX86AtomicCC form = minic_x86_64_atomic_cc_form(
        info->template_text, info->template_length);
    const MinicX86AtomicCCDesc *d = minic_x86_64_atomic_cc_description(form);
    const bool locked = minic_x86_64_atomic_cc_locked_form(
        info->template_text, info->template_length);
    const size_t n = d && d->has_value ? 3U : 2U;
    MinicType mem, flag;
    size_t width, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !b || !info ||
        !d || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != n)
        return false;
    for (i = 0U; i < n; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                          MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &mem) ||
        !minic_type_pointee(fn->values[b[1].value].type, &flag) ||
        !minic_type_equal(flag, minic_type_bool()) ||
        !minic_x86_64_scalar_width(program, target, mem, &width, &sign) ||
        width != d->width ||
        (d->has_value &&
         (!minic_x86_64_scalar_width(
             program, target, fn->values[b[2].value].type, &width, &sign) ||
          width != d->width)))
        return false;
    if ((d->has_value && !x86_value_load(out, frame, b[2].value, "%r10")) ||
        !x86_value_load(out, frame, b[0].value, "%r11"))
        return false;
    if (locked && fputs("  .pushsection .smp_locks,\"a\"\n"
              "  .balign 4\n"
              "  .long 671f - .\n"
              "  .popsection\n"
              "671:\n", out) == EOF)
        return false;
    if (d->has_value) {
        if (fprintf(out, "  %s%s %s, (%%r11)\n",
                locked ? "lock; " : "", d->opcode,
                d->width == 4U ? "%r10d" : "%r10") < 0)
            return false;
    } else if (fprintf(out, "  %s%s (%%r11)\n",
                       locked ? "lock; " : "", d->opcode) < 0)
        return false;
    return fputs(d->flag_sign ? "  sets %al\n" : "  sete %al\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs("  movb %al, (%r10)\n", out) != EOF;
}

/* The only owner of this x86 hardware side effect: CMP sets CF, SBB
 * consumes it immediately to create the branchless speculative mask.
 * Core transports typed SSA values, never a pretend boolean mask. */
static bool emit_nospec_mask(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType output_type;
    size_t width, index_width, output_width, i;
    bool sign, index_signed;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !b || !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "cc") ||
        !minic_x86_64_asm_nospec_template(info->template_text, info->template_length))
        return false;
    for (i = 0U; i < 3U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_unsigned_long()) ||
        !minic_x86_64_scalar_width(
            program, target, output_type, &output_width, &sign) ||
        output_width != 8U ||
        !minic_x86_64_scalar_width(
            program, target, fn->values[b[1].value].type, &width, &sign) ||
        !minic_x86_64_scalar_width(
            program, target, fn->values[b[2].value].type,
            &index_width, &index_signed) ||
        width != index_width || (width != 4U && width != 8U))
        return false;
    if (!x86_value_load(out, frame, b[1].value, "%r10") ||
        !x86_value_load(out, frame, b[2].value, "%r11"))
        return false;
    if (fputs(width == 8U ? "  cmpq %r10, %r11\n" :
                              "  cmpl %r10d, %r11d\n", out) == EOF ||
        fputs("  sbbq %r8, %r8\n", out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r9"))
        return false;
    return fputs("  movq %r8, (%r9)\n", out) != EOF;
}

/* A GNU p argument holds the address of the per-cpu field, not the
 * field's contents. GS base is added by x86 hardware on the actual load.
 * The caller's normal C result lvalue is written only after the GS read;
 * neither a plain Core LOAD nor generic textual inline asm is equivalent. */
static bool emit_percpu_stable_u64(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t width;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !minic_x86_64_asm_percpu_stable_template(
            info->template_text, info->template_length) ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_x86_64_scalar_width(
            program, target, type, &width, &sign) ||
        width != minic_x86_64_asm_percpu_stable_width(
            info->template_text, info->template_length) ||
        !minic_type_is_pointer(fn->values[b[1].value].type))
        return false;
    /* x86-64 SysV RAX/R10/R11 are caller-saved and Core SSA is spilled.
     * The original GNU %P operand can be materialized as an address in R10;
     * %gs:(R10) gives exactly GS-base + per-cpu symbol/field displacement. */
    {
        const char *const suffix = width == 1U ? "b" : width == 2U ? "w" :
                                   width == 4U ? "l" : "q";
        const char *const accumulator = width == 1U ? "%al" :
            width == 2U ? "%ax" : width == 4U ? "%eax" : "%rax";
        return x86_value_load(out, frame, b[1].value, "%r10") &&
               fprintf(out, "  mov%s %%gs:(%%r10), %s\n",
                       suffix, accumulator) >= 0 &&
               x86_value_load(out, frame, b[0].value, "%r11") &&
               fprintf(out, "  mov%s %s, (%%r11)\n",
                       suffix, accumulator) >= 0;
    }
}
static bool emit_muldiv_u64(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i, bytes;
    bool sign;
    if (ins->value.structured_inline_asm.operand_count != 4U ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "rdx") ||
        info->has_memory_clobber ||
        !minic_x86_64_asm_muldiv_template(info->template_text, info->template_length))
        return false;
    for (i = 0U; i < 4U; ++i) {
        MinicType type;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                                   : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(type, &type)) return false;
        if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign) ||
            bytes != 8U || !minic_type_is_integer(type)) return false;
    }
    /* Select register alternatives for GNU rm, never RDX. */
    if (!x86_value_load(out, frame, b[2].value, "%r10") ||
        !x86_value_load(out, frame, b[3].value, "%r11") ||
        !x86_value_load(out, frame, b[1].value, "%rax") ||
        fputs("  mulq %r10\n  divq %r11\n", out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r10") ||
        fprintf(out, "  movq %%rax, (%%r10)\n") < 0)
        return false;
    return true;
}

static bool emit_memory_clobber_matched(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const regs[] = {"%r8", "%r9", "%r10", "%rcx"};
    const size_t count = ins->value.structured_inline_asm.operand_count;
    const size_t outputs = count == 7U ? 4U : count == 5U ? 3U :
                           count == 2U ? 1U : 0U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i, bytes;
    bool sign;
    if (outputs == 0U || !info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        !minic_x86_64_asm_matched_template(info->template_text,
                                          info->template_length, outputs))
        return false;
    for (i = 0U; i < count; ++i) {
        MinicType type;
        if (b[i].value >= fn->value_count || b[i].operand_index != i ||
            b[i].has_fixed_register_binding || b[i].early_clobber)
            return false;
        type = fn->values[b[i].value].type;
        if (i < outputs) {
            if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                !minic_type_pointee(type, &type)) return false;
        } else if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
            return false;
        if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign) ||
            (bytes != 8U &&
             !(i >= outputs && bytes == 4U && minic_type_is_integer(type))))
            return false;
    }
    for (i = 0U; i < count-outputs; ++i)
        if (!x86_value_load(out, frame, b[outputs+i].value, regs[i]))
            return false;
    if (!emit_matched_register_template(out, info, outputs)) return false;
    for (i = 0U; i < outputs; ++i)
        if (!x86_value_load(out, frame, b[i].value, "%r11") ||
            fprintf(out, "  movq %s, (%%r11)\n", regs[i]) < 0)
            return false;
    return true;
}

/* The raw-byte Linux idle helpers still have a physical register ABI:
 * MONITOR consumes RAX/RCX/RDX, MWAIT consumes RAX/RCX, and MWAITX uses
 * RAX/RBX/RCX. The B register is SysV callee-saved, so only MWAITX adds a
 * local push/pop; every other input is caller-scratch. In particular STI
 * must remain next to its MWAIT opcode, with no compiler-generated loads
 * or stack adjustment inserted between them. */
static bool emit_linux_monitor_wait(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const regs_acd[] = {"%rax", "%rcx", "%rdx"};
    static const char *const regs_abc[] = {"%rax", "%rbx", "%rcx"};
    static const char *const byte_codes[] = {
        "  .byte 0x0f, 0x01, 0xc8\n",
        "  .byte 0x0f, 0x01, 0xfa\n",
        "  .byte 0x0f, 0x01, 0xc9\n",
        "  .byte 0x0f, 0x01, 0xfb\n",
        "  sti\n  .byte 0x0f, 0x01, 0xc9\n"
    };
    const unsigned int form = minic_x86_64_linux_monitor_wait_form(
        info->template_text, info->template_length);
    const size_t count = form == 1U || form == 2U || form == 4U ? 3U : 2U;
    const char *const *regs = form == 4U ? regs_abc : regs_acd;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!form || !info->is_volatile || info->is_goto ||
        info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != count || !b)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &b[i];
        if (op->operand_index != i ||
            op->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            op->early_clobber || op->has_fixed_register_binding ||
            op->value >= fn->value_count ||
            (!minic_type_is_pointer(fn->values[op->value].type) &&
             !minic_type_is_integer(fn->values[op->value].type)))
            return false;
    }
    if (form == 4U && fputs("  pushq %rbx\n", out) == EOF)
        return false;
    for (i = 0U; i < count; ++i)
        if (!x86_value_load(out, frame, b[i].value, regs[i]))
            return false;
    if (fputs(byte_codes[form - 1U], out) == EOF)
        return false;
    return form != 4U || fputs("  popq %rbx\n", out) != EOF;
}

static bool emit_linux_segment_read(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int selector = minic_x86_64_linux_segment_read_form(
        info->template_text, info->template_length);
    const bool short_selector = selector <= 2U || selector >= 7U;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    bool wide;
    static const char *const selector_regs[] = {
        "%es", "%ds", "%fs", "%gs", "%ds", "%es",
        "%ss", "%cs", "%fs", "%gs"
    };
    if (!selector || !info->is_volatile || info->is_goto ||
        (short_selector && !info->has_memory_clobber) ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op[0].operand_index != 0U ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        op[0].early_clobber || op[0].has_fixed_register_binding ||
        op[0].value >= fn->value_count ||
        !minic_type_pointee(fn->values[op[0].value].type, &pointee))
        return false;
    wide = minic_type_equal(pointee, minic_type_unsigned_int());
    if (wide ? (selector > 6U) :
        !minic_type_equal(pointee, minic_type_unsigned_short()) ||
        !short_selector)
        return false;
    /* x86-64 has no generic scalar-expression representation for the
     * segment selector register. The 16-bit caller observes exactly two
     * bytes; the regset 32-bit caller observes zero-extended EAX. A narrow
     * store into `fos` would leave the high 16 bits stale. */
    return fprintf(out, "  mov%s %s, %s\n",
                   wide ? "l" : "w",
                   selector_regs[selector - 1U],
                   wide ? "%eax" : "%ax") >= 0 &&
        x86_value_load(out, frame, op[0].value, "%r11") &&
        fputs(wide ? "  movl %eax, (%r11)\n" :
                     "  movw %ax, (%r11)\n", out) != EOF;
}

static bool emit_linux_segment_load_extable(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const segment_names[] = {
        "%fs", "%gs", "%ds", "%es", "%ss"
    };
    const unsigned int form = minic_x86_64_linux_segment_load_extable_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    if (!out || !fn || !frame || !form || !info->is_volatile ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op[0].operand_index ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        op[0].value >= fn->value_count ||
        op[0].early_clobber || op[0].has_fixed_register_binding ||
        !minic_type_equal(fn->values[op[0].value].type,
            minic_type_unsigned_short()))
        return false;
    if (!x86_value_load(out, frame, op[0].value, "%rax")) return false;
    /* Numeric local labels are re-entrant across inlined callers; each
     * relocation is relative to the location of its own ELF data word. */
    return fprintf(out,
        "1:  movw %%ax, %s\n"
        "2:\n"
        "  .pushsection \"__ex_table\",\"a\"\n"
        "  .balign 4\n"
        "  .long (1b) - .\n"
        "  .long (2b) - .\n"
        "  .long 5\n"
        "  .popsection\n", segment_names[form - 1U]) >= 0;
}

/* LKGS is `f2 0f 00 f7` with a read/write DI operand. Its GNU extable
 * helper finds DI as register number seven; preserve that metadata and the
 * exception-handler writeback to the original C selector lvalue. */
static bool emit_linux_lkgs_extable(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_lkgs_extable_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !b ||
        b[0].operand_index != 0U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
        b[0].early_clobber || b[0].has_fixed_register_binding ||
        b[0].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !minic_type_equal(pointee, minic_type_unsigned_short()))
        return false;
    if (!x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs("  movzwl (%r11), %edi\n"
              "1:  .byte 0xf2, 0x0f, 0x00, 0xf7\n"
              "  .pushsection \"__ex_table\",\"a\"\n"
              "  .balign 4\n"
              "  .long (1b) - .\n"
              "  .long (1b) - .\n"
              "  .long (17 | (7 << 8))\n"
              "  .popsection\n", out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r11"))
        return false;
    return fputs("  movw %di, (%r11)\n", out) != EOF;
}

static bool emit_linux_fsgsbase(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const mnemonics[] = {
        "rdfsbase", "rdgsbase", "wrfsbase", "wrgsbase"
    };
    const unsigned int form = minic_x86_64_linux_fsgsbase_form(
        info->template_text, info->template_length);
    const bool reading = form <= 2U;
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    if (!out || !fn || !frame || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op[0].operand_index || op[0].early_clobber ||
        op[0].has_fixed_register_binding || op[0].value >= fn->value_count ||
        op[0].kind != (reading ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
        return false;
    if (reading) {
        if (!minic_type_pointee(fn->values[op[0].value].type, &pointee) ||
            !minic_type_equal(pointee, minic_type_unsigned_long()))
            return false;
        return fprintf(out, "  %s %%rax\n", mnemonics[form - 1U]) >= 0 &&
               x86_value_load(out, frame, op[0].value, "%r11") &&
               fputs("  movq %rax, (%r11)\n", out) != EOF;
    }
    if (!minic_type_equal(fn->values[op[0].value].type,
            minic_type_unsigned_long()) ||
        !x86_value_load(out, frame, op[0].value, "%rax"))
        return false;
    return fprintf(out, "  %s %%rax\n", mnemonics[form - 1U]) >= 0;
}

static bool emit_linux_segment_register_extable(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const segment_names[] = {"%es", "%ds", "%ss"};
    const unsigned int form =
        minic_x86_64_linux_segment_register_extable_form(
            info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    if (!out || !fn || !frame || !form || !info->is_volatile ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !b ||
        b[0].operand_index ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
        b[0].value >= fn->value_count || b[0].early_clobber ||
        b[0].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !minic_type_equal(pointee, minic_type_unsigned_short()))
        return false;
    /* GNU %k0 names the 32-bit view of the selected GPR. Choose EAX and
     * encode RAX=0 in type-17 extable metadata; never mark a different
     * register than the one whose value is actually used by the MOV. */
    if (!x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs("  movzwl (%r11), %eax\n", out) == EOF ||
        fprintf(out,
            "1:  movl %%eax, %s\n"
            "  .pushsection \"__ex_table\",\"a\"\n"
            "  .balign 4\n"
            "  .long (1b) - .\n"
            "  .long (1b) - .\n"
            "  .long (17 | (0 << 8))\n"
            "  .popsection\n", segment_names[form - 1U]) < 0 ||
        !x86_value_load(out, frame, b[0].value, "%r11"))
        return false;
    return fputs("  movw %ax, (%r11)\n", out) != EOF;
}

/* GNU asm-goto kernel nofault MOVs use a faulting instruction followed by
 * a three-word exception entry whose second PREL32 reloc points to the C
 * recovery label. Output loads are committed on the successful fallthrough
 * only. The opaque __large_struct input is the address, never a C record
 * value to copy. Preserve 1/2/4/8-byte transfer width exactly. */
static bool emit_linux_kernel_nofault_goto(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    size_t function_id) {
    const unsigned int form = minic_x86_64_linux_kernel_nofault_goto_form(
        info->template_text, info->template_length);
    const bool load = form <= 4U;
    const size_t width = (size_t)1U << ((form - 1U) % 4U);
    const char suffix = width == 1U ? 'b' : width == 2U ? 'w' :
                        width == 4U ? 'l' : 'q';
    const char *reg = width == 1U ? "%al" : width == 2U ? "%ax" :
                      width == 4U ? "%eax" : "%rax";
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type, carrier;
    size_t bytes;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !form ||
        !info->is_goto || !info->is_volatile || !info->has_memory_clobber ||
        info->register_clobber_count ||
        info->goto_target >= fn->block_count ||
        info->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        b[0].operand_index || b[1].operand_index != 1U ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].kind != (load ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        !minic_type_pointee(fn->values[b[1].value].type, &carrier) ||
        !minic_type_is_record(carrier))
        return false;
    type = fn->values[b[0].value].type;
    if (load && !minic_type_pointee(type, &type)) return false;
    if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign) ||
        bytes != width)
        return false;
    if (!x86_value_load(out, frame, b[1].value, "%r10")) return false;
    if (!load && !x86_value_load(out, frame, b[0].value, "%rax"))
        return false;
    if (fprintf(out, "1:  mov%c %s, %s\n", suffix,
                load ? "(%r10)" : reg,
                load ? reg : "(%r10)") < 0 ||
        fprintf(out,
            "  .pushsection \"__ex_table\",\"a\"\n"
            "  .balign 4\n"
            "  .long (1b) - .\n"
            "  .long .Lmini_x86_f%zu_b%u - .\n"
            "  .long 3\n"
            "  .popsection\n",
            function_id, (unsigned)info->goto_target) < 0)
        return false;
    if (!load) return true;
    return x86_value_load(out, frame, b[0].value, "%r11") &&
        fprintf(out, "  mov%c %s, (%%r11)\n", suffix, reg) >= 0;
}

static bool emit_linux_rep_movsb_alternative(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *stack;
    MinicType value;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_rep_movsb_alternative_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "rax") ||
        ins->value.structured_inline_asm.operand_count != 4U || !b)
        return false;
    for (i = 0U; i < 4U; ++i) {
        if (b[i].operand_index != i ||
            b[i].kind != (i == 3U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) ||
            b[i].early_clobber || b[i].value >= fn->value_count ||
            (i != 3U && !minic_type_pointee(
                fn->values[b[i].value].type, &value)) ||
            (i == 0U || i == 3U ?
                !minic_type_equal(i == 3U ? fn->values[b[i].value].type : value,
                                  minic_type_unsigned_long()) :
                !minic_type_is_pointer(value)) ||
            b[i].has_fixed_register_binding != (i == 3U))
            return false;
    }
    if (b[3].fixed_register_binding_id >= fn->fixed_register_binding_count)
        return false;
    stack = &fn->fixed_register_bindings[b[3].fixed_register_binding_id];
    if (!stack->register_name ||
        strcmp(stack->register_name, "rsp") != 0)
        return false;
    /* The three live GNU input/output registers are machine state across
     * both REP MOVSB and its ALTERNATIVE CALL. RSP is an invariant fixed
     * dependency; no C load/store of its shadow operand is performed. */
    if (!x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs("  movq (%r11), %rcx\n", out) == EOF ||
        !x86_value_load(out, frame, b[1].value, "%r11") ||
        fputs("  movq (%r11), %rdi\n", out) == EOF ||
        !x86_value_load(out, frame, b[2].value, "%r11") ||
        fputs("  movq (%r11), %rsi\n", out) == EOF ||
        fputs(
            "1:\n"
            "661: rep movsb\n"
            "662:\n"
            "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                "((6651f-6641f)-(662b-661b)),0x90\n"
            "663:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .long 661b - .\n"
            "  .long 6641f - .\n"
            "  .4byte (((1 << 0) << 16) | ((18*32+4)))\n"
            "  .byte 663b-661b\n"
            "  .byte 6651f-6641f\n"
            "  .popsection\n"
            "  .pushsection .altinstr_replacement,\"ax\"\n"
            "6641: call rep_movs_alternative\n"
            "6651:\n"
            "  .popsection\n"
            "2:\n"
            "  .pushsection \"__ex_table\",\"a\"\n"
            "  .balign 4\n"
            "  .long (1b) - .\n"
            "  .long (2b) - .\n"
            "  .long 3\n"
            "  .popsection\n", out) == EOF)
        return false;
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rcx, (%r11)\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r11") &&
           fputs("  movq %rdi, (%r11)\n", out) != EOF &&
           x86_value_load(out, frame, b[2].value, "%r11") &&
           fputs("  movq %rsi, (%r11)\n", out) != EOF;
}

/* The kernel's type-20 exception handler decodes the faulting MOV and
 * reconstructs its loaded word across a page boundary. Emit a real 8-byte
 * MOV into RAX and a type-20 PREL32 record; a synthetic all-zero fallback
 * or generic type-3 handler would violate Linux string-copy semantics. */
static bool emit_linux_load_unaligned_zeropad(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_load_unaligned_zeropad_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()) ||
        !minic_type_pointee(fn->values[b[1].value].type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()))
        return false;
    return x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs(
               "1: movq (%r10), %rax\n"
               "2:\n"
               "  .pushsection \"__ex_table\",\"a\"\n"
               "  .balign 4\n"
               "  .long (1b) - .\n"
               "  .long (2b) - .\n"
               "  .long 20\n"
               "  .popsection\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}

static bool emit_linux_lar_selector(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_lar_selector_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_int()) ||
        !minic_type_equal(fn->values[b[1].value].type,
                          minic_type_unsigned_short()))
        return false;
    return x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs("  lar %r10w, %eax\n"
                 "  jz 1f\n"
                 "  xorl %eax, %eax\n"
                 "1:\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

static bool emit_linux_random_seed(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_random_seed_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t i;
    if (!out || !fn || !frame || !info || !form ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U || !b)
        return false;
    for (i = 0U; i < 2U; ++i)
        if (b[i].operand_index != i ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            b[i].value >= fn->value_count || b[i].early_clobber ||
            b[i].has_fixed_register_binding ||
            !minic_type_pointee(fn->values[b[i].value].type, &type) ||
            !minic_type_equal(type, i == 0U ?
                minic_type_bool() : minic_type_unsigned_long()))
            return false;
    /* setc captures CF before *any* address calculation or output store.
     * The output word and the C bool are separate writable objects. */
    return fputs(form == 1U ? "  rdrand %rax\n" :
                               "  rdseed %rax\n", out) != EOF &&
           fputs("  setc %cl\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movb %cl, (%r11)\n", out) != EOF;
}

static bool emit_linux_rep_stosb_alternative(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *stack;
    MinicType type;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_rep_stosb_alternative_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U || !b)
        return false;
    for (i = 0U; i < 4U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber ||
            b[i].kind != (i < 2U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            b[i].has_fixed_register_binding != (i == 2U))
            return false;
        if (i < 2U) {
            if (!minic_type_pointee(fn->values[b[i].value].type, &type) ||
                (i == 0U ?
                    !minic_type_equal(type, minic_type_unsigned_long()) :
                    !minic_type_is_pointer(type)))
                return false;
        } else if (i == 2U) {
            if (!minic_type_equal(fn->values[b[i].value].type,
                                  minic_type_unsigned_long()))
                return false;
        } else if (!minic_type_is_integer(fn->values[b[i].value].type)) {
            return false;
        }
    }
    if (b[2].fixed_register_binding_id >= fn->fixed_register_binding_count)
        return false;
    stack = &fn->fixed_register_bindings[b[2].fixed_register_binding_id];
    if (!stack->register_name || strcmp(stack->register_name, "rsp") != 0)
        return false;
    if (!x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs("  movq (%r11), %rcx\n", out) == EOF ||
        !x86_value_load(out, frame, b[1].value, "%r11") ||
        fputs("  movq (%r11), %rdi\n", out) == EOF ||
        !x86_value_load(out, frame, b[3].value, "%rax") ||
        fputs(
            "1:\n"
            "661: rep stosb\n"
            "662:\n"
            "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                "((6651f-6641f)-(662b-661b)),0x90\n"
            "663:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .long 661b - .\n"
            "  .long 6641f - .\n"
            "  .4byte (((1 << 0) << 16) | ((12*32+11)))\n"
            "  .byte 663b-661b\n"
            "  .byte 6651f-6641f\n"
            "  .popsection\n"
            "  .pushsection .altinstr_replacement,\"ax\"\n"
            "6641: call rep_stos_alternative\n"
            "6651:\n"
            "  .popsection\n"
            "2:\n"
            "  .pushsection \"__ex_table\",\"a\"\n"
            "  .balign 4\n"
            "  .long (1b) - .\n"
            "  .long (2b) - .\n"
            "  .long 3\n"
            "  .popsection\n", out) == EOF)
        return false;
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rcx, (%r11)\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r11") &&
           fputs("  movq %rdi, (%r11)\n", out) != EOF;
}

/* Shared Linux STAC/CLAC alternative patch record. Both have an empty
 * baseline; the exact SMAP CPU feature 308 activates the 3-byte patch. */
static bool emit_linux_smap_patch_zero(FILE *out, bool enable) {
    return fputs(
        "661:\n"
        "662:\n"
        "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
            "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        "  .pushsection .altinstructions,\"a\"\n"
        "  .long 661b - .\n"
        "  .long 6641f - .\n"
        "  .4byte (9*32+20)\n"
        "  .byte 663b-661b\n"
        "  .byte 6651f-6641f\n"
        "  .popsection\n"
        "  .pushsection .altinstr_replacement,\"ax\"\n"
        "6641:\n", out) != EOF &&
        fputs(enable ? "  stac\n" : "  clac\n", out) != EOF &&
        fputs("6651:\n  .popsection\n", out) != EOF;
}

static bool emit_linux_fpu_user_frame(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_fpu_user_frame_form(
        info->template_text, info->template_length);
    static const char *const names[] = {
        "fnsave", "fxsave", "fxsaveq", "fxrstor", "fxrstorq", "frstor"
    };
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type, memory_type;
    size_t i;
    if (!out || !fn || !frame || !info || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U || !b)
        return false;
    for (i = 0U; i < 4U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
                i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                          MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_int()) ||
        !minic_type_pointee(fn->values[b[1].value].type, &memory_type) ||
        !minic_type_is_record(memory_type) ||
        !minic_type_pointee(fn->values[b[3].value].type, &type) ||
        !minic_type_equal(type, memory_type) ||
        !minic_type_is_integer(fn->values[b[2].value].type))
        return false;
    if (!x86_value_load(out, frame, b[2].value, "%rax") ||
        !x86_value_load(out, frame, b[1].value, "%r10") ||
        !emit_linux_smap_patch_zero(out, true) ||
        fprintf(out, "1: %s (%%r10)\n", names[form - 1U]) < 0 ||
        (form == 1U && fputs("  fwait\n", out) == EOF) ||
        fputs("2:\n", out) == EOF ||
        !emit_linux_smap_patch_zero(out, false) ||
        fputs(
            "  .pushsection \"__ex_table\",\"a\"\n"
            "  .balign 4\n"
            "  .long (1b) - .\n"
            "  .long (2b) - .\n"
            "  .long 15\n"
            "  .popsection\n", out) == EOF)
        return false;
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

/* A restore fault must rewrite the chosen output GPR with -EFAULT.
 * Allocating the GNU =r result to EAX pins the architecture register
 * number to zero in Linux's type-17 exception data word. */
static bool emit_linux_task_size_alt(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    static const uint64_t expected[] = {
        UINT64_C(0), UINT64_C(0x7ffffffff000), UINT64_C(0xfffffffffff000)
    };
    MinicType output_type;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_task_size_alt_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U || !b ||
        b[0].operand_index != 0U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[0].value >= fn->value_count || b[0].early_clobber ||
        b[0].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_unsigned_long())) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicCoreInstruction *constant;
        const MinicCoreValueId id = b[i + 1U].value;
        if (b[i + 1U].operand_index != i + 1U ||
            b[i + 1U].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            id >= fn->value_count || b[i + 1U].early_clobber ||
            b[i + 1U].has_fixed_register_binding ||
            fn->values[id].definition >= fn->instruction_count) return false;
        constant = &fn->instructions[fn->values[id].definition];
        if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            (uint64_t)constant->value.integer_value != expected[i]) return false;
    }
    /* Both MOVABS encodings occupy ten bytes, so Linux can patch either
     * alternative in place. Copy the selected register result to the C
     * destination only after the alternatives' control record closes. */
    return fputs(
        "661:\n  movabsq $0x7ffffffff000, %r10\n"
        "662:\n"
        "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n  .pushsection .altinstructions,\"a\"\n"
        "  .long 661b - .\n  .long 6641f - .\n"
        "  .4byte (16*32+16)\n"
        "  .byte 663b-661b\n  .byte 6651f-6641f\n"
        "  .popsection\n"
        "  .pushsection .altinstr_replacement,\"ax\"\n"
        "6641:\n  movabsq $0xfffffffffff000, %r10\n"
        "6651:\n  .popsection\n", out) != EOF &&
        x86_value_load(out, frame, b[0].value, "%r11") &&
        fputs("  movq %r10, (%r11)\n", out) != EOF;
}

static bool emit_linux_unwind_context(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const reads[] = {
        "  leaq (%rip), %r10\n", "  movq %rsp, %r10\n",
        "  movq %rbp, %r10\n"
    };
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_unwind_context_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U || !b)
        return false;
    for (i = 0U; i < 3U; ++i) {
        MinicType type;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            !minic_type_pointee(fn->values[b[i].value].type, &type) ||
            !minic_type_equal(type, minic_type_unsigned_long())) return false;
    }
    for (i = 0U; i < 3U; ++i) {
        if (fputs(reads[i], out) == EOF ||
            !x86_value_load(out, frame, b[i].value, "%r11") ||
            fputs("  movq %r10, (%r11)\n", out) == EOF) return false;
    }
    return true;
}

static bool emit_linux_mxcsr(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_mxcsr_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType plain;
    if (!out || !fn || !frame || !info || form == 0U ||
        !info->is_volatile || info->is_goto || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !b ||
        b[0].operand_index != 0U || b[0].early_clobber ||
        b[0].has_fixed_register_binding || b[0].value >= fn->value_count ||
        b[0].kind != (form == 1U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) ||
        !minic_type_pointee(fn->values[b[0].value].type, &plain) ||
        !minic_type_unqualified(plain, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_int())) return false;
    return x86_value_load(out, frame, b[0].value, "%r10") &&
        fprintf(out, "  %s (%%r10)\n", form == 1U ? "ldmxcsr" : "stmxcsr") >= 0;
}

static bool emit_linux_x87_fild_prepare(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_x87_fild_prepare_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        ins->value.structured_inline_asm.operand_count != 1U || !b ||
        b[0].operand_index != 0U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].value >= fn->value_count || b[0].early_clobber ||
        b[0].has_fixed_register_binding ||
        !minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_is_record(type)) return false;
    return x86_value_load(out, frame, b[0].value, "%r10") &&
        fputs("  fnclex\n  emms\n  fildl (%r10)\n", out) != EOF;
}

static bool emit_linux_fpu_kernel_restore(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const opcodes[] = {"fxrstor", "fxrstorq", "frstor"};
    const unsigned int form = minic_x86_64_linux_fpu_kernel_restore_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType output_type, input_type;
    if (!out || !fn || !frame || !info || form == 0U ||
        form > sizeof(opcodes)/sizeof(opcodes[0]) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].early_clobber || b[1].early_clobber ||
        !minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_pointee(fn->values[b[1].value].type, &input_type) ||
        !minic_type_is_record(input_type) ||
        !minic_type_equal(output_type, input_type)) return false;
    return x86_value_load(out, frame, b[1].value, "%r10") &&
        fprintf(out, "1: %s (%%r10)\n2:\n", opcodes[form - 1U]) >= 0 &&
        fputs("  .pushsection \"__ex_table\",\"a\"\n"
              "  .balign 4\n"
              "  .long (1b) - .\n"
              "  .long (2b) - .\n"
              "  .long 6\n"
              "  .popsection\n", out) != EOF;
}

static bool emit_linux_fxrstor_safe(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_fxrstor_safe_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type, mem;
    size_t i;
    if (!out || !fn || !frame || !info || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U || !b)
        return false;
    for (i = 0U; i < 4U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
                i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                          MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_int()) ||
        !minic_type_pointee(fn->values[b[1].value].type, &mem) ||
        !minic_type_is_record(mem) ||
        !minic_type_pointee(fn->values[b[3].value].type, &type) ||
        !minic_type_equal(type, mem) ||
        !minic_type_is_integer(fn->values[b[2].value].type))
        return false;
    return x86_value_load(out, frame, b[2].value, "%rax") &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           fprintf(out, "1: fxrstor%s (%%r10)\n2:\n",
                   form == 2U ? "q" : "") >= 0 &&
           fputs(
               "  .pushsection \"__ex_table\",\"a\"\n"
               "  .balign 4\n"
               "  .long (1b) - .\n"
               "  .long (2b) - .\n"
               "  .long (17 | ((-14) << 16))\n"
               "  .popsection\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

/* The opcode is bound to RDI by its actual ModRM byte. Emit that exact byte
 * sequence after materializing the four GNU inputs in their fixed registers;
 * EAX becomes zero only after the faultable state transfer completes. */
static bool emit_linux_xstate_fault(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_xstate_fault_form(
        info->template_text, info->template_length);
    static const char *const opcodes[] = {
        "  .byte 0x48,0x0f,0xae,0x27\n",
        "  .byte 0x48,0x0f,0xae,0x2f\n",
        "  .byte 0x48,0x0f,0xc7,0x1f\n",
        "  .byte 0x48,0x0f,0xc7,0x2f\n"
    };
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    MinicType type, memory_type;
    size_t i;
    if (!out || !fn || !frame || !info || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 5U || !b)
        return false;
    for (i = 0U; i < 5U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != kinds[i])
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_int()) ||
        !minic_type_is_pointer(fn->values[b[1].value].type) ||
        !minic_type_pointee(fn->values[b[2].value].type, &memory_type) ||
        !minic_type_is_record(memory_type) ||
        !minic_type_is_integer(fn->values[b[3].value].type) ||
        !minic_type_is_integer(fn->values[b[4].value].type))
        return false;
    return x86_value_load(out, frame, b[1].value, "%rdi") &&
           x86_value_load(out, frame, b[3].value, "%rax") &&
           x86_value_load(out, frame, b[4].value, "%rdx") &&
           fputs("1:\n", out) != EOF &&
           fputs(opcodes[form - 1U], out) != EOF &&
           fputs("  xorl %eax, %eax\n2:\n"
                 "  .pushsection \"__ex_table\",\"a\"\n"
                 "  .balign 4\n"
                 "  .long (1b) - .\n"
                 "  .long (2b) - .\n"
                 "  .long 15\n"
                 "  .popsection\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

static bool emit_linux_xrstor_alternative(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    MinicType type;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_xrstor_alternative_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 4U || !b)
        return false;
    for (i = 0U; i < 4U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != kinds[i])
            return false;
    if (!minic_type_is_pointer(fn->values[b[0].value].type) ||
        !minic_type_pointee(fn->values[b[1].value].type, &type) ||
        !minic_type_is_record(type) ||
        !minic_type_is_integer(fn->values[b[2].value].type) ||
        !minic_type_is_integer(fn->values[b[3].value].type))
        return false;
    return x86_value_load(out, frame, b[0].value, "%rdi") &&
           x86_value_load(out, frame, b[2].value, "%rax") &&
           x86_value_load(out, frame, b[3].value, "%rdx") &&
           fputs(
               "661: .byte 0x48,0x0f,0xae,0x2f\n"
               "662:\n"
               "  .skip -(((6651f-6641f)-(662b-661b))>0)*"
                   "((6651f-6641f)-(662b-661b)),0x90\n"
               "663:\n"
               "  .pushsection .altinstructions,\"a\"\n"
               "  .long 661b - .\n"
               "  .long 6641f - .\n"
               "  .4byte (10*32+3)\n"
               "  .byte 663b-661b\n"
               "  .byte 6651f-6641f\n"
               "  .popsection\n"
               "  .pushsection .altinstr_replacement,\"ax\"\n"
               "6641: .byte 0x48,0x0f,0xc7,0x1f\n"
               "6651:\n"
               "  .popsection\n"
               "3:\n"
               "  .pushsection \"__ex_table\",\"a\"\n"
               "  .balign 4\n"
               "  .long (661b) - .\n"
               "  .long (3b) - .\n"
               "  .long 6\n"
               "  .popsection\n", out) != EOF;
}

/* XSAVE64's three feature patches are all four bytes, so the expanded
 * maximum-length padding expression is zero. Keep all three patch records
 * and preserve the type-17 error-GPR entry. EAX is selected for the generic
 * =r output, making the register-number field in the exception data zero. */
static bool emit_linux_xsave_triple_alternative(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    MinicType type, memory_type;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_xsave_triple_alternative_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 5U || !b)
        return false;
    for (i = 0U; i < 5U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != kinds[i])
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_type_equal(type, minic_type_int()) ||
        !minic_type_is_pointer(fn->values[b[1].value].type) ||
        !minic_type_pointee(fn->values[b[2].value].type, &memory_type) ||
        !minic_type_is_record(memory_type) ||
        !minic_type_is_integer(fn->values[b[3].value].type) ||
        !minic_type_is_integer(fn->values[b[4].value].type))
        return false;
    return x86_value_load(out, frame, b[1].value, "%rdi") &&
           x86_value_load(out, frame, b[3].value, "%rax") &&
           x86_value_load(out, frame, b[4].value, "%rdx") &&
           fputs(
               "661: .byte 0x48,0x0f,0xae,0x27\n"
               "662:\n"
               "663:\n"
               "  .pushsection .altinstructions,\"a\"\n"
               "  .long 661b - .\n"
               "  .long 6641f - .\n"
               "  .4byte (10*32+0)\n"
               "  .byte 663b-661b\n"
               "  .byte 6651f-6641f\n"
               "  .long 661b - .\n"
               "  .long 6642f - .\n"
               "  .4byte (10*32+1)\n"
               "  .byte 663b-661b\n"
               "  .byte 6652f-6642f\n"
               "  .long 661b - .\n"
               "  .long 6643f - .\n"
               "  .4byte (10*32+3)\n"
               "  .byte 663b-661b\n"
               "  .byte 6653f-6643f\n"
               "  .popsection\n"
               "  .pushsection .altinstr_replacement,\"ax\"\n"
               "6641: .byte 0x48,0x0f,0xae,0x37\n"
               "6651:\n"
               "6642: .byte 0x48,0x0f,0xc7,0x27\n"
               "6652:\n"
               "6643: .byte 0x48,0x0f,0xc7,0x2f\n"
               "6653:\n"
               "  .popsection\n"
               "  xorl %eax, %eax\n"
               "3:\n"
               "  .pushsection \"__ex_table\",\"a\"\n"
               "  .balign 4\n"
               "  .long (661b) - .\n"
               "  .long (3b) - .\n"
               "  .long (17 | ((-14) << 16))\n"
               "  .popsection\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movl %eax, (%r11)\n", out) != EOF;
}

/* SMAP is CPU-feature conditional; Linux emits a blank baseline and a
 * replacement-only instruction, including STAC/CLAC and full RFLAGS
 * capture/restore. Keep the replacement in .altinstr_replacement and never
 * execute privileged SMAP instructions on unsupported CPUs. */
bool minic_x86_64_emit_linux_smap_alternative(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_smap_alternative_form(
        info->template_text, info->template_length);
    const MinicCoreStructuredInlineAsmOperand *b = ins &&
        ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ?
        ins->value.structured_inline_asm.operands : NULL;
    MinicType pointee;
    if (!out || !info || !form) return false;
    if (form < 3U) {
        if (!info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count)
            return false;
    } else {
    if (!info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "cc") ||
        ins->value.structured_inline_asm.operand_count != 1U || !b ||
        b[0].operand_index != 0U || b[0].value >= fn->value_count ||
        b[0].kind != (form == 3U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        b[0].early_clobber || b[0].has_fixed_register_binding)
        return false;
    if (form == 3U) {
        if (!minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
            !minic_type_equal(pointee, minic_type_unsigned_long()))
            return false;
    } else if (!minic_type_equal(fn->values[b[0].value].type,
                                 minic_type_unsigned_long())) {
        return false;
    }
    }
    if (form == 4U && !x86_value_load(out, frame, b[0].value, "%rax"))
        return false;
    if (fputs(
            "661:\n"
            "662:\n"
            "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                "((6651f-6641f)-(662b-661b)),0x90\n"
            "663:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .long 661b - .\n"
            "  .long 6641f - .\n"
            "  .4byte (9*32+20)\n"
            "  .byte 663b-661b\n"
            "  .byte 6651f-6641f\n"
            "  .popsection\n"
            "  .pushsection .altinstr_replacement,\"ax\"\n"
            "6641:\n", out) == EOF)
        return false;
    if (fputs(form == 1U ? "  clac\n" :
              form == 2U ? "  stac\n" :
              form == 3U ? "  pushfq\n  popq %rax\n  clac\n" :
                           "  pushq %rax\n  popfq\n", out) == EOF ||
        fputs("6651:\n  .popsection\n", out) == EOF)
        return false;
    return form != 3U ||
        (x86_value_load(out, frame, b[0].value, "%r11") &&
         fputs("  movq %rax, (%r11)\n", out) != EOF);
}

/* Linux uaccess put-user helper is a non-SysV CALL with in/out RCX,
 * input RAX, live RSP and an explicit EBX clobber. A 16-byte save area
 * protects callee-saved RBX WITHOUT losing 16-byte alignment before CALL.
 * The helper itself owns exception-table recovery; a plain MOV to user
 * memory would bypass the fault fixup and return the wrong error code. */
static bool emit_linux_put_user_call(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_put_user_call_form(
        info->template_text, info->template_length);
    unsigned int width;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *stack_binding, *value_binding;
    const MinicCoreInstruction *size_constant;
    MinicType result_type;
    size_t i, value_width;
    bool signed_type;
    if (!out || !program || !target || !fn || !frame || !info ||
        (form != 0x200U && form != 0x800U) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "ebx") ||
        ins->value.structured_inline_asm.operand_count != 5U || !b)
        return false;
    for (i = 0U; i < 5U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            b[i].has_fixed_register_binding != (i == 1U || i == 3U))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &result_type) ||
        !minic_type_equal(result_type, minic_type_int()) ||
        !minic_type_is_pointer(fn->values[b[2].value].type) ||
        !minic_x86_64_scalar_width(program, target,
            fn->values[b[3].value].type, &value_width, &signed_type) ||
        !minic_type_is_integer(
            fn->values[b[4].value].type) ||
        b[1].fixed_register_binding_id >= fn->fixed_register_binding_count ||
        b[3].fixed_register_binding_id >= fn->fixed_register_binding_count)
        return false;
    stack_binding = &fn->fixed_register_bindings[b[1].fixed_register_binding_id];
    value_binding = &fn->fixed_register_bindings[b[3].fixed_register_binding_id];
    if (!stack_binding->register_name ||
        strcmp(stack_binding->register_name, "rsp") != 0 ||
        !value_binding->register_name ||
        (strcmp(value_binding->register_name, "rax") != 0 &&
         strcmp(value_binding->register_name, "%rax") != 0))
        return false;
    if (fn->values[b[4].value].definition >= fn->instruction_count)
        return false;
    size_constant = &fn->instructions[fn->values[b[4].value].definition];
    if (size_constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        size_constant->result != b[4].value ||
        (size_constant->value.integer_value != 1 &&
         size_constant->value.integer_value != 2 &&
         size_constant->value.integer_value != 4 &&
         size_constant->value.integer_value != 8))
        return false;
    width = (unsigned int)size_constant->value.integer_value;
    if (value_width != width) return false;
    /* Keep the original RCX/RAX value loading after saving RBX. The
     * output address remains in the spill frame and is reloaded only once
     * the helper has returned its error code in ECX. */
    if (fputs("  subq $16, %rsp\n  movq %rbx, (%rsp)\n", out) == EOF ||
        !x86_value_load(out, frame, b[2].value, "%rcx") ||
        !x86_value_load(out, frame, b[3].value, "%rax") ||
        fprintf(out, "  call __put_user_%s%u\n",
                form == 0x800U ? "nocheck_" : "", width) < 0 ||
        fputs("  movq (%rsp), %rbx\n  addq $16, %rsp\n", out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs("  movl %ecx, (%r11)\n", out) == EOF)
        return false;
    return true;
}

/* __get_user_N and __get_user_nocheck_N use a distinct kernel helper ABI:
 * RAX carries the user address and returns a signed int error; RDX carries
 * the fetched 1/2/4/8-byte value. Both output bindings are observable C
 * lvalues and are written back at their true widths. The original +r RSP
 * binding is physical and must never be loaded from a synthetic SSA slot. */
static bool emit_linux_get_user_call(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const value_registers[] = {
        "%dl", "%dx", "%edx", "%rdx"
    };
    static const char *const store_suffix[] = {"b", "w", "l", "q"};
    const unsigned int form = minic_x86_64_linux_get_user_call_form(
        info->template_text, info->template_length);
    const bool nocheck = form == 4U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFixedRegisterBinding *stack_binding, *result_binding;
    const MinicCoreInstruction *size_constant;
    MinicType error_type, value_type;
    size_t i, width, suffix_index;
    bool signed_type;
    if (!out || !program || !target || !fn || !frame || !info ||
        (form != 3U && form != 4U) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 5U || !b)
        return false;
    for (i = 0U; i < 5U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber ||
            b[i].kind != (i < 2U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            b[i].has_fixed_register_binding != (i == 1U || i == 2U))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &error_type) ||
        !minic_type_equal(error_type, minic_type_int()) ||
        !minic_type_pointee(fn->values[b[1].value].type, &value_type) ||
        !minic_x86_64_scalar_width(program, target,
            value_type, &width, &signed_type) ||
        !minic_type_is_integer(value_type) ||
        !minic_type_is_pointer(fn->values[b[3].value].type) ||
        !minic_type_is_integer(fn->values[b[4].value].type) ||
        b[1].fixed_register_binding_id >= fn->fixed_register_binding_count ||
        b[2].fixed_register_binding_id >= fn->fixed_register_binding_count)
        return false;
    result_binding = &fn->fixed_register_bindings[b[1].fixed_register_binding_id];
    stack_binding = &fn->fixed_register_bindings[b[2].fixed_register_binding_id];
    if (!result_binding->register_name ||
        (strcmp(result_binding->register_name, "rdx") != 0 &&
         strcmp(result_binding->register_name, "%rdx") != 0) ||
        !stack_binding->register_name ||
        strcmp(stack_binding->register_name, "rsp") != 0 ||
        fn->values[b[4].value].definition >= fn->instruction_count)
        return false;
    size_constant = &fn->instructions[fn->values[b[4].value].definition];
    if (size_constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        size_constant->result != b[4].value ||
        size_constant->value.integer_value != (int64_t)width)
        return false;
    switch (width) {
    case 1U: suffix_index = 0U; break;
    case 2U: suffix_index = 1U; break;
    case 4U: suffix_index = 2U; break;
    case 8U: suffix_index = 3U; break;
    default: return false;
    }
    return x86_value_load(out, frame, b[3].value, "%rax") &&
        fprintf(out, "  call __get_user%s_%zu\n",
            nocheck ? "_nocheck" : "", width) >= 0 &&
        x86_value_load(out, frame, b[1].value, "%r11") &&
        fprintf(out, "  mov%s %s, (%%r11)\n",
            store_suffix[suffix_index], value_registers[suffix_index]) >= 0 &&
        x86_value_load(out, frame, b[0].value, "%r11") &&
        fputs("  movl %eax, (%r11)\n", out) != EOF;
}

static bool emit_linux_idle_clflush_alt(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *zero;
    size_t i;
    if (!out || !fn || !frame || !info || !info->is_volatile ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        !minic_x86_64_linux_idle_clflush_alt_form(
            info->template_text, info->template_length, true))
        return false;
    for (i = 0U; i < 2U; ++i)
        if (b[i].operand_index != i ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].value >= fn->value_count)
            return false;
    if (!minic_type_is_integer(fn->values[b[0].value].type) ||
        !minic_type_is_pointer(fn->values[b[1].value].type) ||
        fn->values[b[0].value].definition >= fn->instruction_count)
        return false;
    zero = &fn->instructions[fn->values[b[0].value].definition];
    if (zero->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        zero->result != b[0].value || zero->value.integer_value != 0)
        return false;
    /* The original section bytes use PREL32 offsets relative to the
     * location of each .long, with the original NOP range padded to the
     * CLFLUSH replacement's exact instruction length. */
    return x86_value_load(out, frame, b[1].value, "%rax") &&
        fputs(
            "661:\n"
            "662:\n"
            "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                "((6651f-6641f)-(662b-661b)),0x90\n"
            "663:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .long 661b - .\n"
            "  .long 6641f - .\n"
            "  .4byte (22*32 + (7))\n"
            "  .byte 663b-661b\n"
            "  .byte 6651f-6641f\n"
            "  .popsection\n"
            "  .pushsection .altinstr_replacement,\"ax\"\n"
            "6641:\n"
            "  clflush (%rax)\n"
            "6651:\n"
            "  .popsection\n", out) != EOF;
}

static bool emit_linux_pat_clflushopt(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *zero;
    MinicType pointee;
    if (!out || !fn || !frame || !info || !info->is_volatile ||
        info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        !minic_x86_64_linux_pat_clflushopt_form(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
        !minic_type_unqualified(pointee, &pointee) ||
        pointee.base_kind != MINIC_TYPE_BASE_INT ||
        pointee.integer_rank != MINIC_INTEGER_RANK_CHAR ||
        pointee.pointer_depth != 0U ||
        fn->values[b[1].value].definition >= fn->instruction_count)
        return false;
    zero = &fn->instructions[fn->values[b[1].value].definition];
    if (zero->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        zero->result != b[1].value || zero->value.integer_value != 0)
        return false;
    /* Original ALTERNATIVE_2 relative 32-bit references and the paired
       old/replacement lengths are emitted intact for the kernel patcher. */
    return x86_value_load(out, frame, b[0].value, "%rax") &&
        fputs(
            "661:\n"
            "  .byte 0x3e\n"
            "  clflush (%rax)\n"
            "662:\n"
            "  .skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                 "((6651f-6641f)-(662b-661b)),0x90\n"
            "663:\n"
            "  .pushsection .altinstructions,\"a\"\n"
            "  .long 661b - .\n"
            "  .long 6641f - .\n"
            "  .4byte (9*32+23)\n"
            "  .byte 663b-661b\n"
            "  .byte 6651f-6641f\n"
            "  .popsection\n"
            "  .pushsection .altinstr_replacement,\"ax\"\n"
            "6641:\n"
            "  .byte 0x66\n"
            "  clflush (%rax)\n"
            "6651:\n"
            "  .popsection\n", out) != EOF;
}

/* Linux futex uaccess ASM has a real type-17 exception-table register
 * recovery contract. The fault handler writes -EFAULT to R8D (architectural
 * extable register number 8), which we use for the GNU `ret` output. Both
 * instruction sites in a cmpxchg retry loop carry their own table entries. */
static bool x86_emit_futex_extable_record(FILE *out,
                                         unsigned int first_label,
                                         unsigned int recovery_label) {
    return fprintf(out,
        "  .pushsection \"__ex_table\",\"a\"\n"
        "  .balign 4\n"
        "  .long (%ub) - .\n"
        "  .long (%ub) - .\n"
        "  .long (17 | ((-14) << 16)) + (8 << 8)\n"
        "  .popsection\n", first_label, recovery_label) >= 0;
}

static bool emit_linux_futex_extable(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const unsigned int form = minic_x86_64_linux_futex_extable_form(
        info->template_text, info->template_length);
    const bool loop = form >= 4U;
    const size_t outputs = loop ? 4U : 3U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || form == 0U ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count || !b ||
        ins->value.structured_inline_asm.operand_count != outputs + 2U)
        return false;
    for (i = 0U; i < outputs + 2U; ++i) {
        const bool memory = i == 2U;
        const bool readwrite = i == 0U && form == 1U;
        const MinicCoreStructuredInlineAsmOperandKind expected = memory ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i < outputs ?
                (readwrite ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].operand_index != i || b[i].kind != expected ||
            b[i].has_fixed_register_binding || b[i].value >= fn->value_count)
            return false;
        if (i < outputs) {
            if (!minic_type_pointee(fn->values[b[i].value].type, &pointee) ||
                !minic_type_unqualified(pointee, &pointee) ||
                pointee.base_kind != MINIC_TYPE_BASE_INT ||
                pointee.integer_rank != MINIC_INTEGER_RANK_INT ||
                pointee.pointer_depth != 0U) return false;
        } else if (fn->values[b[i].value].type.base_kind != MINIC_TYPE_BASE_INT ||
                   fn->values[b[i].value].type.integer_rank != MINIC_INTEGER_RANK_INT ||
                   fn->values[b[i].value].type.pointer_depth != 0U)
            return false;
    }
    /* Every operand is evaluated into Core values before we enter the ISA
       sequence. `%r10` is the user-memory base; `%r8d` is the physical
       exception-handler return register, never a fabricated C error. */
    if (!x86_value_load(out, frame, b[2].value, "%r10"))
        return false;
    if (form == 1U) {
        if (!x86_value_load(out, frame, b[0].value, "%rcx") ||
            fputs("  movl (%rcx), %r8d\n", out) == EOF ||
            !x86_value_load(out, frame, b[3].value, "%r9") ||
            !x86_value_load(out, frame, b[4].value, "%rax"))
            return false;
    } else if (loop) {
        if (!x86_value_load(out, frame, b[4].value, "%r11") ||
            !x86_value_load(out, frame, b[5].value, "%r8"))
            return false;
    } else {
        if (!x86_value_load(out, frame, b[3].value, "%rax") ||
            !x86_value_load(out, frame, b[4].value, "%r8"))
            return false;
    }

    if (form == 1U || form == 3U || loop) {
        /* SMP lock patch site is the instruction, not the metadata blob. */
        if (fputs("1:\n", out) == EOF) return false;
        if (loop) {
            if (fputs("  movl (%r10), %eax\n"
                      "2:\n"
                      "  movl %eax, %r9d\n", out) == EOF)
                return false;
            if (fprintf(out, "  %sl %%r11d, %%r9d\n",
                        form == 4U ? "or" : form == 5U ? "and" : "xor") < 0 ||
                fputs("3:\n", out) == EOF)
                return false;
        }
        if (fputs("  .pushsection .smp_locks,\"a\"\n"
                  "  .balign 4\n"
                  "  .long 671f - .\n"
                  "  .popsection\n"
                  "671:\n"
                  "  lock; ", out) == EOF)
            return false;
        if (form == 1U || loop) {
            if (fputs("cmpxchgl %r9d, (%r10)\n", out) == EOF)
                return false;
            if (loop && fputs("  jnz 2b\n4:\n", out) == EOF)
                return false;
        } else if (fputs("xaddl %eax, (%r10)\n", out) == EOF)
            return false;
    } else {
        if (fputs("1:\n  xchgl %eax, (%r10)\n", out) == EOF) return false;
    }
    if (!loop && fputs("2:\n", out) == EOF) return false;
    if (!x86_emit_futex_extable_record(out, 1U, loop ? 4U : 2U) ||
        (loop && !x86_emit_futex_extable_record(out, 3U, 4U)))
        return false;

    if (form == 1U) {
        if (!x86_value_load(out, frame, b[0].value, "%rcx") ||
            fputs("  movl %r8d, (%rcx)\n", out) == EOF)
            return false;
        if (!x86_value_load(out, frame, b[1].value, "%rcx") ||
            fputs("  movl %eax, (%rcx)\n", out) == EOF)
            return false;
    } else {
        if (!x86_value_load(out, frame, b[0].value, "%rcx") ||
            fputs("  movl %eax, (%rcx)\n", out) == EOF ||
            !x86_value_load(out, frame, b[1].value, "%rcx") ||
            fputs("  movl %r8d, (%rcx)\n", out) == EOF)
            return false;
        if (loop && (!x86_value_load(out, frame, b[3].value, "%rcx") ||
                     fputs("  movl %r9d, (%rcx)\n", out) == EOF))
            return false;
    }
    return true;
}

static bool emit_linux_verw_clear(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *op =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    if (!out || !fn || !frame || !ins || !info ||
        !minic_x86_64_linux_verw_clear_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "cc") ||
        ins->value.structured_inline_asm.operand_count != 1U || !op ||
        op[0].operand_index != 0U ||
        op[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        op[0].early_clobber || op[0].has_fixed_register_binding ||
        op[0].value >= fn->value_count ||
        !minic_type_pointee(fn->values[op[0].value].type, &pointee) ||
        !minic_type_unqualified(pointee, &pointee) ||
        !minic_type_equal(pointee, minic_type_unsigned_short()))
        return false;
    return x86_value_load(out, frame, op[0].value, "%r11") &&
        fputs("  verw (%r11)\n", out) != EOF;
}

static bool emit_linux_debug_register(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const unsigned int physical[] = {0U, 1U, 2U, 3U, 6U, 7U};
    const unsigned int form = minic_x86_64_linux_debug_register_form(
        info->template_text, info->template_length);
    const bool reading = form <= 6U;
    const bool dr7 = form == 6U || form == 12U;
    const unsigned int index = form ? (form - 1U) % 6U : 0U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t i;
    if (!out || !fn || !frame || !info || !form ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != (dr7 ? 2U : 1U) ||
        !b)
        return false;
    for (i = 0U; i < (dr7 ? 2U : 1U); ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 1U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
                reading ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                          MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (reading) {
        if (!minic_type_pointee(fn->values[b[0].value].type, &type) ||
            !minic_type_equal(type, minic_type_unsigned_long()))
            return false;
    } else if (!minic_type_equal(
            fn->values[b[0].value].type, minic_type_unsigned_long()))
        return false;
    if (dr7 &&
        (!minic_type_pointee(fn->values[b[1].value].type, &type) ||
         !minic_type_equal(type, minic_type_unsigned_int())))
        return false;
    /* DR7's synthetic m input is never dereferenced. This helper emits
     * only the actual MOV to/from a debug register and the true scalar
     * result store. Reading address 0x1000 would fault in kernel mode. */
    if (reading)
        return fprintf(out, "  movq %%db%u, %%rax\n", physical[index]) >= 0 &&
               x86_value_load(out, frame, b[0].value, "%r11") &&
               fputs("  movq %rax, (%r11)\n", out) != EOF;
    return x86_value_load(out, frame, b[0].value, "%rax") &&
           fprintf(out, "  movq %%rax, %%db%u\n", physical[index]) >= 0;
}

/* Linux x86 Protection Keys (PKRU). The kernel's byte literals are exact
 * three-byte instructions, so the compiler owns the fixed EAX/ECX/EDX
 * register transfer and the two separate 32-bit outputs. Do not preserve
 * only EAX for RDPKRU: EDX is an observable GNU output as well, even if a
 * caller later proves its local dead. The host assembler is just the final
 * encoder of our already-qualified machine instruction. */
static bool emit_linux_pkru(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const instructions[] = {
        "  .byte 0x0f,0x01,0xee\n", /* RDPKRU */
        "  .byte 0x0f,0x01,0xef\n"  /* WRPKRU */
    };
    static const MinicCoreStructuredInlineAsmOperandKind read_kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    const unsigned int form = info ? minic_x86_64_linux_pkru_form(
        info->template_text, info->template_length) : 0U;
    const bool read = form == 1U;
    const MinicCoreStructuredInlineAsmOperand *bindings =
        ins->value.structured_inline_asm.operands;
    MinicType scalar;
    size_t i;
    if (!out || !fn || !frame || !ins || !info || !form ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U || !bindings)
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicCoreStructuredInlineAsmOperand *b = &bindings[i];
        const bool output = read && i < 2U;
        if (b->operand_index != i || b->value >= fn->value_count ||
            b->early_clobber || b->has_fixed_register_binding ||
            b->kind != (read ? read_kinds[i] :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        if (output) {
            if (!minic_type_pointee(fn->values[b->value].type, &scalar) ||
                !minic_type_equal(scalar, minic_type_unsigned_int()))
                return false;
        } else if (!minic_type_equal(fn->values[b->value].type,
                                     minic_type_unsigned_int()))
            return false;
    }
    if (read) {
        return x86_value_load(out, frame, bindings[2].value, "%rcx") &&
            fputs(instructions[0], out) != EOF &&
            x86_value_load(out, frame, bindings[0].value, "%r11") &&
            fputs("  movl %eax, (%r11)\n", out) != EOF &&
            x86_value_load(out, frame, bindings[1].value, "%r11") &&
            fputs("  movl %edx, (%r11)\n", out) != EOF;
    }
    return x86_value_load(out, frame, bindings[0].value, "%rax") &&
           x86_value_load(out, frame, bindings[1].value, "%rcx") &&
           x86_value_load(out, frame, bindings[2].value, "%rdx") &&
           fputs(instructions[1], out) != EOF;
}

/* x87 FDIV erratum probe from arch/x86/kernel/fpu/bugs.c. Load all three
 * source-object addresses before touching the x87 stack: the probe has
 * deliberately precise FPU state transitions and must not accidentally
 * use any hidden floating-point C temporary or change FISTP's destination
 * width. The data objects themselves are ordinary C globals/locals whose
 * addresses are already established by shared Core memory lowering. */
static bool emit_linux_fdiv_bug(
    FILE *out, const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
    };
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType pointee;
    size_t i;
    if (!out || !fn || !frame || !info ||
        !minic_x86_64_linux_fdiv_bug_form(
            info->template_text, info->template_length) ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 3U || !b)
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != kinds[i] || b[i].early_clobber ||
            b[i].has_fixed_register_binding ||
            !minic_type_pointee(fn->values[b[i].value].type, &pointee) ||
            !minic_type_equal(pointee, i == 0U ?
                minic_type_int() : minic_type_double()))
            return false;
    }
    return x86_value_load(out, frame, b[0].value, "%r11") &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           x86_value_load(out, frame, b[2].value, "%r9") &&
           fputs("  fninit\n"
                 "  fldl (%r10)\n"
                 "  fdivl (%r9)\n"
                 "  fmull (%r9)\n"
                 "  fldl (%r10)\n"
                 "  fsubp %st, %st(1)\n"
                 "  fistpl (%r11)\n"
                 "  fwait\n"
                 "  fninit\n", out) != EOF;
}

/* Zstd's three CPUID source contracts have only one or three outputs; none
 * ties EAX to an output as native_cpuid does. Recover every hardware role
 * from the qualified exact clobber+operand shape, including the ECX subleaf
 * input for leaf 7. Never let these count-2/count-4 forms fall through to
 * bit-scan, NOSPEC or muldiv machine dispatch. */
static bool emit_zstd_cpuid(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const output_regs[][3] = {
        {"%eax", NULL, NULL},
        {"%eax", "%ecx", "%edx"},
        {"%eax", "%ebx", "%ecx"}
    };
    static const char *const input_regs[][2] = {
        {"%rax", NULL}, {"%rax", NULL}, {"%rax", "%rcx"}
    };
    static const char *const clobbers[][3] = {
        {"ebx", "ecx", "edx"}, {"ebx", NULL, NULL},
        {"edx", NULL, NULL}
    };
    static const size_t output_counts[] = {1U, 3U, 3U};
    static const size_t input_counts[] = {1U, 1U, 2U};
    static const size_t clobber_counts[] = {3U, 1U, 1U};
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    size_t form, i, bytes;
    bool sign;
    if (info->template_length != 5U ||
        memcmp(info->template_text, "cpuid", 5U) != 0 ||
        info->has_memory_clobber ||
        !info->register_clobbers)
        return false;
    for (form = 0U; form < 3U; ++form) {
        if (count != output_counts[form] + input_counts[form] ||
            info->register_clobber_count != clobber_counts[form])
            continue;
        for (i = 0U; i < clobber_counts[form]; ++i)
            if (!clobber_is(&info->register_clobbers[i], clobbers[form][i]))
                break;
        if (i == clobber_counts[form]) break;
    }
    if (form == 3U) return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreValueId id = b[i].value;
        MinicType pointee;
        if (id >= fn->value_count || b[i].operand_index != i ||
            b[i].has_fixed_register_binding || b[i].early_clobber)
            return false;
        if (i < output_counts[form]) {
            if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                !minic_type_pointee(fn->values[id].type, &pointee) ||
                !minic_x86_64_scalar_width(program, target, pointee,
                                            &bytes, &sign) || bytes != 4U)
                return false;
        } else if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                   !minic_x86_64_scalar_width(program, target,
                                               fn->values[id].type,
                                               &bytes, &sign) || bytes != 4U)
            return false;
    }
    /* CPUID overwrites EBX regardless of which outputs are requested.
     * Saving RBX is required by System V x86-64 ABI, even for register
     * clobbers that GNU's inline-asm register allocator could handle. */
    if (fputs("  pushq %rbx\n", out) == EOF) return false;
    for (i = 0U; i < input_counts[form]; ++i)
        if (!x86_value_load(out, frame, b[output_counts[form]+i].value,
                            input_regs[form][i]))
            return false;
    if (fputs("  cpuid\n", out) == EOF) return false;
    for (i = 0U; i < output_counts[form]; ++i)
        if (!x86_value_load(out, frame, b[i].value, "%r10") ||
            fprintf(out, "  movl %s, (%%r10)\n", output_regs[form][i]) < 0)
            return false;
    return fputs("  popq %rbx\n", out) != EOF;
}

bool minic_x86_64_emit_structured_asm(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, size_t function_id) {
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *bindings;
    size_t i, bytes;
    bool sign, cpuid, mulacc;
    static const char *const cpuid_input_regs[4] = {
        "%rax", "%rbx", "%rcx", "%rdx"
    };
    static const char *const cpuid_output_regs[4] = {
        "%eax", "%ebx", "%ecx", "%edx"
    };
    static const char *const mulacc_regs[3] = {"%r8", "%r9", "%rcx"};

    if (!out || !program || !target || !fn || !frame || !ins ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
        return false;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    bindings = ins->value.structured_inline_asm.operands;
    if (!info->template_text)
        return false;
    if (info->template_length == 5U &&
        memcmp(info->template_text, "cpuid", 5U) == 0 &&
        (ins->value.structured_inline_asm.operand_count == 2U ||
         ins->value.structured_inline_asm.operand_count == 4U ||
         ins->value.structured_inline_asm.operand_count == 5U))
        return emit_zstd_cpuid(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_irq_stack_call_form(
            info->template_text, info->template_length))
        return emit_linux_irq_stack_call(out, fn, frame, ins, info);
    if (minic_x86_64_linux_preempt_static_call_form(
            info->template_text, info->template_length))
        return emit_linux_preempt_static_call(out, fn, ins, info);
    if (minic_x86_64_linux_task_control_form(
            info->template_text, info->template_length))
        return emit_linux_task_control(out, fn, frame, ins, info);
    if (minic_x86_64_linux_iret_to_self_form(
            info->template_text, info->template_length))
        return emit_linux_iret_to_self(out, fn, frame, ins, info);
    if (minic_x86_64_linux_int3_selftest_form(
            info->template_text, info->template_length))
        return emit_linux_int3_selftest(out, fn, frame, ins, info);
    if (minic_x86_64_linux_pv_indirect_call_form(
            info->template_text, info->template_length))
        return emit_linux_pv_indirect_call(out, fn, frame, ins, info);
    if (minic_x86_64_linux_alt_reloc_selftest_form(
            info->template_text, info->template_length))
        return emit_linux_alt_reloc_selftest(out, fn, ins, info);
    if (minic_x86_64_linux_real_mode_far_jump_form(
            info->template_text, info->template_length))
        return emit_linux_real_mode_far_jump(out, fn, frame, ins, info);
    if (minic_x86_64_linux_mmio_read_form(
            info->template_text, info->template_length))
        return emit_linux_mmio_read(out, fn, frame, ins, info);
    if (minic_x86_64_linux_mmio_write_form(
            info->template_text, info->template_length))
        return emit_linux_mmio_write(out, fn, frame, ins, info);
    if (minic_x86_64_linux_rep_string_io_form(
            info->template_text, info->template_length))
        return emit_linux_rep_string_io(out, fn, frame, ins, info);
    if (minic_x86_64_linux_iomem_copy_form(
            info->template_text, info->template_length))
        return emit_linux_iomem_copy(out, fn, frame, ins, info);
    if (minic_x86_64_linux_i915_movntdqa_form(
            info->template_text, info->template_length))
        return emit_linux_i915_movntdqa(out, fn, frame, ins, info);
    if (minic_x86_64_linux_flushcache_copy_form(
            info->template_text, info->template_length))
        return emit_linux_flushcache_copy(out, fn, frame, ins, info);
    if (minic_x86_64_linux_atomic_xchg_form(
            info->template_text, info->template_length))
        return emit_linux_atomic_xchg(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_pvclock_scale_form(
            info->template_text, info->template_length))
        return emit_linux_pvclock_scale(out, fn, frame, ins, info);
    if (minic_x86_64_linux_task_size_alt_form(
            info->template_text, info->template_length))
        return emit_linux_task_size_alt(out, fn, frame, ins, info);
    if (minic_x86_64_linux_unwind_context_form(
            info->template_text, info->template_length))
        return emit_linux_unwind_context(out, fn, frame, ins, info);
    if (minic_x86_64_linux_mxcsr_form(
            info->template_text, info->template_length))
        return emit_linux_mxcsr(out, fn, frame, ins, info);
    if (minic_x86_64_linux_pkru_form(
            info->template_text, info->template_length))
        return emit_linux_pkru(out, fn, frame, ins, info);
    if (minic_x86_64_linux_fdiv_bug_form(
            info->template_text, info->template_length))
        return emit_linux_fdiv_bug(out, fn, frame, ins, info);
    if (minic_x86_64_linux_monitor_wait_form(
            info->template_text, info->template_length))
        return emit_linux_monitor_wait(out, fn, frame, ins, info);
    if (minic_x86_64_linux_segment_read_form(
            info->template_text, info->template_length))
        return emit_linux_segment_read(out, fn, frame, ins, info);
    if (minic_x86_64_linux_segment_load_extable_form(
            info->template_text, info->template_length))
        return emit_linux_segment_load_extable(out, fn, frame, ins, info);
    if (minic_x86_64_linux_lkgs_extable_form(
            info->template_text, info->template_length))
        return emit_linux_lkgs_extable(out, fn, frame, ins, info);
    if (minic_x86_64_linux_fsgsbase_form(
            info->template_text, info->template_length))
        return emit_linux_fsgsbase(out, fn, frame, ins, info);
    if (minic_x86_64_linux_kernel_nofault_goto_form(
            info->template_text, info->template_length))
        return emit_linux_kernel_nofault_goto(
            out, program, target, fn, frame, ins, info, function_id);
    if (minic_x86_64_linux_rep_movsb_alternative_form(
            info->template_text, info->template_length))
        return emit_linux_rep_movsb_alternative(out, fn, frame, ins, info);
    if (minic_x86_64_linux_rep_stosb_alternative_form(
            info->template_text, info->template_length))
        return emit_linux_rep_stosb_alternative(out, fn, frame, ins, info);
    if (minic_x86_64_linux_fpu_user_frame_form(
            info->template_text, info->template_length))
        return emit_linux_fpu_user_frame(out, fn, frame, ins, info);
    if (minic_x86_64_linux_x87_fild_prepare_form(
            info->template_text, info->template_length))
        return emit_linux_x87_fild_prepare(out, fn, frame, ins, info);
    if (minic_x86_64_linux_fpu_kernel_restore_form(
            info->template_text, info->template_length))
        return emit_linux_fpu_kernel_restore(out, fn, frame, ins, info);
    if (minic_x86_64_linux_fxrstor_safe_form(
            info->template_text, info->template_length))
        return emit_linux_fxrstor_safe(out, fn, frame, ins, info);
    if (minic_x86_64_linux_xstate_fault_form(
            info->template_text, info->template_length))
        return emit_linux_xstate_fault(out, fn, frame, ins, info);
    if (minic_x86_64_linux_xrstor_alternative_form(
            info->template_text, info->template_length))
        return emit_linux_xrstor_alternative(out, fn, frame, ins, info);
    if (minic_x86_64_linux_xsave_triple_alternative_form(
            info->template_text, info->template_length))
        return emit_linux_xsave_triple_alternative(out, fn, frame, ins, info);
    if (minic_x86_64_linux_load_unaligned_zeropad_form(
            info->template_text, info->template_length))
        return emit_linux_load_unaligned_zeropad(out, fn, frame, ins, info);
    if (minic_x86_64_linux_lar_selector_form(
            info->template_text, info->template_length))
        return emit_linux_lar_selector(out, fn, frame, ins, info);
    if (minic_x86_64_linux_random_seed_form(
            info->template_text, info->template_length))
        return emit_linux_random_seed(out, fn, frame, ins, info);
    if (minic_x86_64_linux_smap_alternative_form(
            info->template_text, info->template_length))
        return minic_x86_64_emit_linux_smap_alternative(
            out, fn, frame, ins, info);
    if (minic_x86_64_linux_segment_register_extable_form(
            info->template_text, info->template_length))
        return emit_linux_segment_register_extable(out, fn, frame, ins, info);
    if (minic_x86_64_linux_put_user_call_form(
            info->template_text, info->template_length))
        return emit_linux_put_user_call(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_get_user_call_form(
            info->template_text, info->template_length))
        return emit_linux_get_user_call(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_idle_clflush_alt_form(
            info->template_text, info->template_length, true))
        return emit_linux_idle_clflush_alt(out, fn, frame, ins, info);
    if (minic_x86_64_linux_pat_clflushopt_form(
            info->template_text, info->template_length))
        return emit_linux_pat_clflushopt(out, fn, frame, ins, info);
    if (minic_x86_64_linux_futex_extable_form(
            info->template_text, info->template_length))
        return emit_linux_futex_extable(out, fn, frame, ins, info);
    if (minic_x86_64_linux_verw_clear_form(
            info->template_text, info->template_length))
        return emit_linux_verw_clear(out, fn, frame, ins, info);
    if (minic_x86_64_linux_debug_register_form(
            info->template_text, info->template_length))
        return emit_linux_debug_register(out, fn, frame, ins, info);
    if (info->template_length == 6U &&
        memcmp(info->template_text, "xsetbv", 6U) == 0)
        return emit_linux_xsetbv(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_fxsave_form(
            info->template_text, info->template_length))
        return emit_linux_fxsave(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_x87_probe_form(
            info->template_text, info->template_length))
        return emit_linux_x87_probe(out, fn, frame, ins, info);
    if (minic_x86_64_linux_alternative_wrmsr_form(
            info->template_text, info->template_length))
        return emit_linux_alternative_wrmsr(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_control_register_form(
            info->template_text, info->template_length))
        return emit_linux_control_register(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_tlb_cache_form(
            info->template_text, info->template_length))
        return emit_linux_tlb_cache(out, fn, frame, ins, info);
    if (minic_x86_64_linux_three_call_alternative_form(
            info->template_text, info->template_length))
        return emit_linux_three_call_alternative(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_boot_segments_template(
            info->template_text, info->template_length)) {
        const MinicCoreStructuredInlineAsmOperand *operand = bindings;
        size_t width;
        if (!operand || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count != 0U ||
            ins->value.structured_inline_asm.operand_count != 1U ||
            operand[0].operand_index != 0U ||
            operand[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            operand[0].has_fixed_register_binding || operand[0].early_clobber ||
            operand[0].value >= fn->value_count ||
            !minic_x86_64_scalar_width(program, target,
                fn->values[operand[0].value].type, &width, &sign) ||
            (width != 2U && width != 4U && width != 8U)) return false;
        /* GNU's movl %%eax,%%ds/ss/es is an architectural selector load.
         * Only the 16-bit selector participates; unlike a C assignment to
         * memory this changes live CPU state and SS can inhibit interrupts
         * until the following instruction. Keep original operation order. */
        return x86_value_load(out, frame, operand[0].value, "%rax") &&
               fputs("  movw %ax, %ds\n"
                     "  movw %ax, %ss\n"
                     "  movw %ax, %es\n", out) != EOF;
    }
    {
        const unsigned int table = minic_x86_64_linux_descriptor_table_form(
            info->template_text, info->template_length);
        if (table != 0U) {
        const MinicCoreStructuredInlineAsmOperand *operand = bindings;
        MinicType desc_type;
        size_t size;
        if (!operand || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count != 0U ||
            ins->value.structured_inline_asm.operand_count != 1U ||
            operand[0].operand_index != 0U ||
            operand[0].kind != (table >= 3U
                                   ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
                                   : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
            operand[0].early_clobber || operand[0].has_fixed_register_binding ||
            operand[0].value >= fn->value_count ||
            !minic_type_pointee(fn->values[operand[0].value].type, &desc_type) ||
            !minic_type_is_record(desc_type) ||
            !minic_target_info_sizeof_type(target, program, desc_type, &size) ||
            size != 10U)
            return false;
        /* Memory input is the Core lvalue address. LIDT atomically consumes
         * the packed 10-byte descriptor; no fabricated doubleword load or
         * user-mode software emulation is permitted. */
        return x86_value_load(out, frame, operand[0].value, "%r11") &&
               fputs(table == 1U ? "  lidt (%r11)\n" :
                     table == 2U ? "  lgdt (%r11)\n" :
                     table == 3U ? "  sgdt (%r11)\n" :
                                   "  sidt (%r11)\n", out) != EOF;
        }
    }
    {
        const unsigned int gs_bit = minic_x86_64_linux_gs_testcc_form(
            info->template_text, info->template_length);
        if (gs_bit != 0U) {
            const MinicCoreStructuredInlineAsmOperand *b = bindings;
            MinicType flag_type, mem_type;
            size_t width;
            if (!info->is_volatile || info->is_goto ||
                !info->has_memory_clobber || info->register_clobber_count ||
                ins->value.structured_inline_asm.operand_count != 3U ||
                b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
                b[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                b[0].operand_index != 0U || b[1].operand_index != 1U ||
                b[2].operand_index != 2U ||
                b[0].value >= fn->value_count ||
                b[1].value >= fn->value_count ||
                b[2].value >= fn->value_count ||
                !minic_type_pointee(fn->values[b[0].value].type, &flag_type) ||
                !minic_type_equal(flag_type, minic_type_bool()) ||
                !minic_type_pointee(fn->values[b[1].value].type, &mem_type) ||
                !minic_x86_64_scalar_width(
                    program, target, mem_type, &width, &sign) ||
                (gs_bit == 1U ? (width != 4U && width != 8U)
                              : width != 8U)) return false;
            return x86_value_load(out, frame, b[1].value, "%r11") &&
                   x86_value_load(out, frame, b[2].value, "%r10") &&
                   fprintf(out, "  bt%c %s, %%gs:(%%r11)\n",
                           gs_bit == 1U ? 'l' : 'q',
                           gs_bit == 1U ? "%r10d" : "%r10") >= 0 &&
                   fputs("  setc %al\n", out) != EOF &&
                   x86_value_load(out, frame, b[0].value, "%r11") &&
                   fputs("  movb %al, (%r11)\n", out) != EOF;
        }
    }
    {
        const unsigned int testcc = minic_x86_64_linux_testcc_form(
            info->template_text, info->template_length);
        if (testcc != 0U) {
            const MinicCoreStructuredInlineAsmOperand *b = bindings;
            MinicType result_type, memory_type;
            if (!info->is_volatile || info->is_goto ||
                !info->has_memory_clobber || info->register_clobber_count ||
                ins->value.structured_inline_asm.operand_count != 3U ||
                b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
                b[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                b[0].operand_index != 0U || b[1].operand_index != 1U ||
                b[2].operand_index != 2U ||
                b[0].value >= fn->value_count ||
                b[1].value >= fn->value_count ||
                b[2].value >= fn->value_count ||
                !minic_type_pointee(fn->values[b[0].value].type, &result_type) ||
                !minic_type_equal(result_type, minic_type_bool()) ||
                !minic_type_pointee(fn->values[b[1].value].type, &memory_type) ||
                !minic_type_is_integer(memory_type)) return false;
            return x86_value_load(out, frame, b[1].value, "%r11") &&
                   x86_value_load(out, frame, b[2].value, "%r10") &&
                   fputs("  testb %r10b, (%r11)\n", out) != EOF &&
                   fputs(testcc == 1U ? "  setnz %al\n" : "  setz %al\n", out) != EOF &&
                   x86_value_load(out, frame, b[0].value, "%r11") &&
                   fputs("  movb %al, (%r11)\n", out) != EOF;
        }
    }
    {
        const unsigned int form = minic_x86_64_linux_zero_barrier_form(
            info->template_text, info->template_length);
        if (form != 0U) {
            static const char *const emit[] = {
                "  lock; addl $0, -4(%rsp)\n",
                "  mfence\n", "  lfence\n", "  sfence\n",
                "  pause\n", "  rep; nop\n", "  fninit\n"
            };
            if (!info->is_volatile || info->is_goto ||
                ins->value.structured_inline_asm.operand_count != 0U ||
                (form <= 4U && !info->has_memory_clobber) ||
                (form == 1U
                 ? info->register_clobber_count != 1U ||
                   info->register_clobbers == NULL ||
                   !clobber_is(&info->register_clobbers[0], "cc")
                 : info->register_clobber_count != 0U))
                return false;
            return fputs(emit[form - 1U], out) != EOF;
        }
    }
    if (minic_x86_64_linux_ip_here_template(
            info->template_text, info->template_length)) {
        MinicType pointee;
        if (info->is_goto || info->register_clobber_count != 0U ||
            info->has_memory_clobber ||
            ins->value.structured_inline_asm.operand_count != 1U ||
            bindings[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            bindings[0].operand_index != 0U ||
            bindings[0].value >= fn->value_count ||
            !minic_type_pointee(fn->values[bindings[0].value].type, &pointee) ||
            (!minic_type_equal(pointee, minic_type_unsigned_long()) &&
             !minic_type_equal(pointee, minic_type_long()) &&
             !minic_type_equal(pointee, minic_type_unsigned_long_long()) &&
             !minic_type_equal(pointee, minic_type_long_long())))
            return false;
        /* GNU LEA obtains the address of its own following instruction.
         * Emit the same PC-relative machine semantics, then write the
         * result into the Core address-backed C output local. */
        return fputs("  leaq 0(%rip), %rax\n", out) != EOF &&
            x86_value_load(out, frame, bindings[0].value, "%r11") &&
            fputs("  movq %rax, (%r11)\n", out) != EOF;
    }
    {
        unsigned int percpu_op=0U;size_t percpu_width=0U;
        if (minic_x86_64_linux_percpu_form(info->template_text,
              info->template_length,&percpu_op,&percpu_width))
            return emit_linux_percpu_gs(out,program,target,fn,frame,ins,info);
    }
    if(minic_x86_64_linux_prefetchw_alt_form(
          info->template_text,info->template_length))
        return emit_linux_prefetchw_alt(out,fn,frame,ins,info);
    if(minic_x86_64_linux_apic_amd_alt_form(
          info->template_text,info->template_length))
        return emit_linux_apic_amd_alt(out,fn,frame,ins,info);
    { size_t gs_tied_width=0U;
      if(minic_x86_64_gs_tied_alu_form(
             info->template_text,info->template_length,&gs_tied_width))
          return emit_linux_gs_tied_alu(out,program,target,fn,frame,ins,info);
    }
    if (minic_x86_64_add32_carry_template(
            info->template_text,info->template_length))
        return emit_add32_carry(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_tcpudp_nofold_template(
            info->template_text,info->template_length))
        return emit_linux_tcpudp_nofold(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_ip_fast_csum_template(
            info->template_text,info->template_length))
        return emit_ip_fast_csum(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_checksum_fold_template(
            info->template_text,info->template_length))
        return emit_checksum_fold(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_msr_form(
            info->template_text,info->template_length))
        return emit_linux_msr(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_hweight_alternative_form(
            info->template_text,info->template_length))
        return emit_hweight_alternative(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_counter_form(
            info->template_text,info->template_length))
        return emit_x86_counter(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_port_io_form(
            info->template_text,info->template_length)!=MINIC_X86_PORT_IO_NONE)
        return emit_linux_port_io(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_smp_xadd_width(
            info->template_text, info->template_length))
        return emit_linux_local_xadd(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_local_xadd_width(
            info->template_text, info->template_length))
        return emit_linux_local_xadd(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_percpu_xadd_width(
            info->template_text, info->template_length))
        return emit_linux_local_xadd(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_gs_cmpxchg_width(
            info->template_text, info->template_length))
        return emit_linux_gs_cmpxchg(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_rep_stos_width(
            info->template_text, info->template_length))
        return emit_linux_rep_stos(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_lowlevel_scalar_form(
            info->template_text, info->template_length))
        return emit_linux_lowlevel_scalar(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_boot_flags_form(
            info->template_text, info->template_length) &&
        !(minic_x86_64_linux_boot_flags_form(
              info->template_text, info->template_length) == 3U &&
          ins->value.structured_inline_asm.operand_count == 8U))
        return emit_linux_boot_flags(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_delay_mul_form(
            info->template_text, info->template_length))
        return emit_linux_delay_mul(out, fn, frame, ins, info);
    if (minic_x86_64_linux_delay_wait_form(
            info->template_text, info->template_length))
        return emit_linux_delay_wait(out, fn, frame, ins, info);
    if (minic_x86_64_linux_identity_load_form(
            info->template_text, info->template_length))
        return emit_linux_identity_load(out, fn, frame, ins, info);
    if (minic_x86_64_linux_ipv6_checksum_form(
            info->template_text, info->template_length))
        return emit_linux_ipv6_checksum(out, fn, frame, ins, info);
    if (minic_x86_64_linux_lock_bitcc_form(
            info->template_text, info->template_length))
        return emit_linux_lock_bitcc(
            out, program, target, fn, frame, ins, info);
    if (minic_x86_64_linux_boot_string_form(
            info->template_text, info->template_length))
        return emit_linux_boot_string(out, fn, frame, ins, info);
    if (minic_x86_64_linux_vdso_syscall_form(
            info->template_text, info->template_length))
        return emit_linux_vdso_syscall(out, fn, frame, ins, info);
    if (minic_x86_64_linux_vdso_cpunode_form(
            info->template_text, info->template_length))
        return emit_linux_vdso_cpunode(out, fn, frame, ins, info);
    if (minic_x86_64_linux_csum_partial_form(
            info->template_text, info->template_length))
        return emit_linux_csum_partial(out, program, target, fn, frame, ins, info);
    if (minic_x86_64_atomic_rmw_form(
            info->template_text,info->template_length)!=MINIC_X86_ATOMIC_RMW_NONE)
        return emit_linux_atomic_rmw(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_cmpxchg128_form(
            info->template_text, info->template_length))
        return emit_linux_cmpxchg128(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_raw_cmpxchg_form(
            info->template_text,info->template_length)!=MINIC_X86_TRYCAS_NONE)
        return emit_linux_raw_cmpxchg(
            out,program,target,fn,frame,ins,info);
    if (minic_x86_64_trycas_form(
            info->template_text,info->template_length)!=MINIC_X86_TRYCAS_NONE)
        return emit_linux_trycas(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_bitops_form(
            info->template_text,info->template_length)!=
        MINIC_X86_BITOPS_NONE)
        return emit_linux_bitops(
            out,program,target,fn,frame,ins,info);
    if (minic_x86_64_atomic_cc_form(
            info->template_text,info->template_length)!=
        MINIC_X86_ATOMIC_CC_NONE)
        return emit_linux_atomic_cc(
            out,program,target,fn,frame,ins,info);
    /* Preserve the pre-existing memory-clobber family (including its
     * valid two-operand dec_mod shape). Only DISJOINT no-clobber,
     * register-output two-operand templates enter the BSF/BSR selector.
     * This dispatch is by semantic contract, never operand count alone. */
    /* An exact source-qualified Linux native_cpuid carries a "memory"
     * compiler barrier and SIX typed operands, unlike the legacy 8-operand
     * CPUID. It is NOT a generic memory-clobber matched-ASM instruction.
     * Preserve this target-only ownership boundary and let the shared
     * CPUID machine verifier below check all six physical register roles. */
    if (info->has_memory_clobber &&
        !(ins->value.structured_inline_asm.operand_count == 6U &&
          info->template_length == 5U &&
          memcmp(info->template_text, "cpuid", 5U) == 0))
        return info->is_volatile &&
               emit_memory_clobber_matched(
                   out, program, target, fn, frame, ins, info);
    if ((ins->value.structured_inline_asm.operand_count == 2U ||
         ins->value.structured_inline_asm.operand_count == 3U) &&
        (minic_x86_64_asm_bit_scan_form(
             info->template_text, info->template_length) ==
             MINIC_X86_BIT_SCAN_BSF32_TIED ||
         minic_x86_64_asm_bit_scan_form(
             info->template_text, info->template_length) ==
             MINIC_X86_BIT_SCAN_BSR32_TIED ||
         minic_x86_64_asm_bit_scan_form(
             info->template_text, info->template_length) ==
             MINIC_X86_BIT_SCAN_BSR64_READWRITE))
        return emit_linux_tied_bit_scan(
            out, program, target, fn, frame, ins, info);
    if (ins->value.structured_inline_asm.operand_count == 2U &&
        info->register_clobber_count == 0U) {
        if (minic_x86_64_asm_percpu_stable_template(
                info->template_text, info->template_length))
            return emit_percpu_stable_u64(
                out, program, target, fn, frame, ins, info);
        return emit_bit_scan_u64(out, program, target, fn, frame, ins, info);
    }
    if (ins->value.structured_inline_asm.operand_count == 3U)
        return emit_nospec_mask(out, program, target, fn, frame, ins, info);
    if (!info->is_volatile)
        return false;
    if (ins->value.structured_inline_asm.operand_count == 4U)
        return emit_muldiv_u64(out, program, target, fn, frame, ins, info);
    if (ins->value.structured_inline_asm.operand_count != 8U &&
        ins->value.structured_inline_asm.operand_count != 6U)
        return false;
    cpuid = info->template_length == 5U &&
        memcmp(info->template_text, "cpuid", 5U) == 0 &&
        info->register_clobber_count == 0U &&
        (ins->value.structured_inline_asm.operand_count == 6U ?
         info->has_memory_clobber : !info->has_memory_clobber);
    mulacc = ins->value.structured_inline_asm.operand_count == 8U &&
        info->register_clobber_count == 2U &&
        info->register_clobbers != NULL &&
        ((clobber_is(&info->register_clobbers[0], "cc") &&
          clobber_is(&info->register_clobbers[1], "dx")) ||
         (clobber_is(&info->register_clobbers[0], "dx") &&
          clobber_is(&info->register_clobbers[1], "cc"))) &&
        minic_x86_64_asm_mulacc_template(info->template_text,
                                         info->template_length);
    if (!cpuid && !mulacc) return false;

    /* Revalidate the target frame layout and register/value kinds. A future
     * generic structured Core producer cannot bypass this machine contract. */
    for (i = 0U;
         i < ins->value.structured_inline_asm.operand_count; ++i) {
        MinicType pointee;
        const MinicCoreValueId id = bindings[i].value;
        if (id >= fn->value_count || bindings[i].operand_index != i ||
            bindings[i].has_fixed_register_binding || bindings[i].early_clobber)
            return false;
        if (i < (cpuid ? 4U : 3U)) {
            if (bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                !minic_type_pointee(fn->values[id].type, &pointee) ||
                !minic_x86_64_scalar_width(program, target, pointee,
                                            &bytes, &sign) ||
                bytes != (cpuid ? 4U : 8U)) return false;
        } else if (mulacc && i == 7U) {
            if (bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
                !minic_type_pointee(fn->values[id].type, &pointee) ||
                !minic_x86_64_scalar_width(program, target, pointee,
                                            &bytes, &sign) || bytes != 8U)
                return false;
        } else {
            if (bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                !minic_x86_64_scalar_width(program, target, fn->values[id].type,
                                            &bytes, &sign) ||
                bytes != (cpuid ? 4U : 8U)) return false;
        }
    }
    if (cpuid) {
        /* RBX is callee-preserved even though CPUID overwrites EBX.
         * Keep it on the stack until all four output stores complete. */
        if (fputs("  pushq %rbx\n", out) == EOF) return false;
        for (i = 0U;
             i < ins->value.structured_inline_asm.operand_count - 4U; ++i)
            if (!x86_value_load(
                    out, frame, bindings[i+4U].value,
                    cpuid_input_regs[
                        ins->value.structured_inline_asm.operand_count == 6U &&
                        i == 1U ? 2U : i]))
                return false;
        if (fputs("  cpuid\n", out) == EOF) return false;
        for (i = 0U; i < 4U; ++i)
            if (!x86_value_load(out, frame, bindings[i].value, "%r10") ||
                fprintf(out, "  movl %s, (%%r10)\n",
                        cpuid_output_regs[i]) < 0) return false;
        return fputs("  popq %rbx\n", out) != EOF;
    }

    /* 64x64 -> 128 product in RDX:RAX, added into three 64-bit
     * accumulators with carry. Select the register side of GNU '=rm'.
     * Do not allocate RDX, the explicitly clobbered hardware register. */
    if (!x86_value_load(out, frame, bindings[3].value, "%r8") ||
        !x86_value_load(out, frame, bindings[4].value, "%r9") ||
        !x86_value_load(out, frame, bindings[5].value, "%rcx") ||
        !x86_value_load(out, frame, bindings[6].value, "%rax") ||
        !x86_value_load(out, frame, bindings[7].value, "%r11") ||
        !emit_mulacc_template(out, info)) return false;
    for (i = 0U; i < 3U; ++i)
        if (!x86_value_load(out, frame, bindings[i].value, "%r10") ||
            fprintf(out, "  movq %s, (%%r10)\n", mulacc_regs[i]) < 0)
            return false;
    return true;
}
