#include "target/x86_64/asm_contract.h"
#include <stdio.h>
#include <string.h>
/* All GNU %c inputs have already been resolved into owned symbols/numbers
 * by the shared Core operand normalizer. Preserve the Linux BUG/WARN metadata
 * in its original section, including the actual x86 UD2 instruction, numeric
 * local labels, and PC32 relocations. This is a deliberately narrow grammar:
 * arbitrary opaque inline ASM and additional instructions remain rejected. */
bool minic_x86_64_linux_bug_table_opaque(const char *text, size_t length) {
    size_t cursor = 0U;
    bool ud2 = false, bug_section = false, address = false;
    bool padding = false, section_end = false;
    if (text == NULL || length < 70U || length > 2048U ||
        strlen(text) != length)
        return false;
    while (cursor < length) {
        char line[256];
        size_t begin = cursor, n, i;
        char *p, *comment, *end;
        while (cursor < length && text[cursor] != '\n') ++cursor;
        n = cursor - begin;
        if (cursor < length) ++cursor;
        if (n >= sizeof(line)) return false;
        (void)memcpy(line, text + begin, n);
        line[n] = '\0';
        p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
        comment = strchr(p, '#');
        if (comment != NULL) *comment = '\0';
        end = p + strlen(p);
        while (end > p && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r')) --end;
        *end = '\0';
        if (*p == '\0') continue;
        if (p[0] == '1' && p[1] == ':') {
            p += 2;
            while (*p == ' ' || *p == '\t') ++p;
        } else if (p[0] == '2' && p[1] == ':') {
            p += 2;
            while (*p == ' ' || *p == '\t') ++p;
        } else if (strcmp(p, "998:") == 0) {
            continue;
        }
        if (strncmp(p, ".byte", 5U) == 0) {
            char compact[64]; size_t m = 0U;
            for (i = 5U; p[i] != '\0'; ++i) {
                if (p[i] == ' ' || p[i] == '\t') continue;
                if (m + 1U >= sizeof(compact)) return false;
                compact[m++] = p[i];
            }
            compact[m] = '\0';
            if (strcmp(compact, "0x0f,0x0b") != 0 || ud2) return false;
            ud2 = true;
        } else if (strcmp(p, ".pushsection __bug_table,\"aw\"") == 0) {
            if (bug_section) return false;
            bug_section = true;
        } else if (strcmp(p, ".pushsection .discard.reachable") == 0) {
            if (!section_end) return false;
        } else if (strncmp(p, ".long ", 6U) == 0) {
            if (!bug_section || strchr(p, '%') != NULL ||
                strchr(p, ';') != NULL) return false;
            if (strstr(p, "1b") != NULL) address = true;
        } else if (strncmp(p, ".word ", 6U) == 0) {
            if (!bug_section || strchr(p, '%') != NULL ||
                strchr(p, ';') != NULL) return false;
        } else if (strncmp(p, ".org 2b+", 8U) == 0) {
            if (!bug_section || padding) return false;
            for (i = 8U; p[i] != '\0'; ++i)
                if (p[i] < '0' || p[i] > '9') return false;
            if (i == 8U) return false;
            padding = true;
        } else if (strcmp(p, ".popsection") == 0) {
            section_end = true;
        } else {
            return false;
        }
    }
    return ud2 && bug_section && address && padding && section_end;
}


/* Target-owned GNU register/constraint admission: no Core lowering state,
 * no SSA mutation. Additional x86 forms extend this owner, not core_lower. */
static bool target_constraint_is(const MinicInlineAsmOperand *, const char *);
static bool clobber_is(const MinicInlineAsmRegisterClobber *, const char *);
unsigned int minic_x86_64_linux_testcc_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "testb%2,%1/*outputconditioncodenz*/",
        "testb%2,%1/*outputconditioncodez*/"
    };
    char compact[120];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

/* The per-CPU bit test reads GS-relative storage. It cannot be lowered
 * through an ordinary C pointer dereference: GS is an architectural base,
 * and the GNU flag output observes CF after BT. Preserve both effects. */
unsigned int minic_x86_64_linux_gs_testcc_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "btl%%gs:%2,%1/*outputconditioncodec*/",
        "btq%%gs:%2,%1/*outputconditioncodec*/"
    };
    char compact[120];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_gs_testcc_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? minic_x86_64_linux_gs_testcc_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *flag, *memory, *index;
    MinicType plain;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 1U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->label_count ||
        src->has_memory_clobber || src->clobber_count != 0U ||
        src->register_clobber_count != 0U ||
        !target_constraint_is(&src->outputs[0], "=@ccc") ||
        !target_constraint_is(&src->inputs[0], "m") ||
        !target_constraint_is(&src->inputs[1], "Ir")) return false;
    flag = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[0].expression);
    index = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!flag || !memory || !index ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(flag->type) ||
        !minic_type_unqualified(flag->type, &plain) ||
        !minic_type_equal(plain, minic_type_bool()) ||
        !minic_type_unqualified(memory->type, &plain) ||
        !minic_type_is_integer(plain) ||
        !minic_type_is_integer(index->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_testcc_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? minic_x86_64_linux_testcc_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *flag, *memory, *mask;
    MinicType type;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 1U || src->input_count != 2U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], form == 1U ? "=@ccnz" : "=@ccz") ||
        !target_constraint_is(&src->inputs[0], "m") ||
        !target_constraint_is(&src->inputs[1], "i")) return false;
    flag = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[0].expression);
    mask = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!flag || !memory || !mask ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(flag->type) ||
        !minic_type_unqualified(flag->type, &type) ||
        !minic_type_equal(type, minic_type_bool()) ||
        !minic_type_unqualified(memory->type, &type) ||
        !minic_type_is_integer(type) ||
        !minic_type_is_integer(mask->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Keep the GS per-CPU source reserved, without capturing ordinary MOV. */
bool minic_x86_64_requires_qualified_input_asm(const MinicInlineAsm *src) {
    static const char *const reserved[] = {"movq %%gs:", "cmp",
        "outb ", "outw ", "outl ", "inb ", "inw ", "inl ", "lidt", "lgdt"};
    return minic_backend_inline_asm_prefix_matches(
        src, reserved, sizeof(reserved) / sizeof(reserved[0]));
}

/* LGDT/LIDT do not read an integer value: their operand is the actual
 * ten-byte packed descriptor (u16 limit, u64 base). Linux boot and IDT
 * setup use this privileged memory form, not a pointer-valued register.
 * A different struct or width would make the CPU read beyond the operand. */
unsigned int minic_x86_64_linux_descriptor_table_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "lidt %0", "lgdt %0", "sgdt %0", "sidt %0"
    };
    size_t i;
    if (!text) return 0U;
    for (i = 0U; i < sizeof(exact) / sizeof(exact[0]); ++i)
        if (length == strlen(exact[i]) &&
            memcmp(text, exact[i], length) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
}

/* Strictly recognize an entire privileged template, not a prefix or an
 * arbitrary assembler string. GNU's whitespace does not change operands. */
static bool x86_linux_compact_asm_equals(const char *text, size_t length,
                                        const char *expected) {
    size_t i, j = 0U;
    if (!text || !expected) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == '\0') return false;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (expected[j] != c) return false;
        ++j;
    }
    return expected[j] == '\0';
}

unsigned int minic_x86_64_linux_task_control_form(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length, "ltr%w0")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "str%0")) return 2U;
    return 0U;
}

bool minic_x86_64_linux_iret_to_self_form(const char *text, size_t length) {
    return x86_linux_compact_asm_equals(
        text, length, "mov%%ss,%0pushq%q0pushq%%rspaddq$8,(%%rsp)"
                      "pushfqmov%%cs,%0pushq%q0pushq$1firetq1:");
}

bool minic_x86_64_linux_int3_selftest_form(const char *text, size_t length) {
    return x86_linux_compact_asm_equals(
        text, length, "int3_selftest_ip:986:.pushsection.discard.noendbr"
                      ".long986b.popsectionint3;nop;nop;nop;nop");
}

/* Paravirtualized Linux IRQ safe halt: executable indirect CALL and retained
 * parainstruction site/length metadata are an indivisible patching ABI. */
bool minic_x86_64_linux_pv_indirect_call_form(const char *text, size_t length) {
    static const char expected[] =
        "771:999:.pushsection.discard.retpoline_safe.long999b.popsection"
        "call*%[paravirt_opptr];772:"
        ".pushsection.parainstructions,\"a\".balign8.quad771b"
        ".byte%c[paravirt_typenum].byte772b-771b.balign8.popsection";
    static const char numeric[] =
        "771:999:.pushsection.discard.retpoline_safe.long999b.popsection"
        "call*%6;772:"
        ".pushsection.parainstructions,\"a\".balign8.quad771b"
        ".byte%c5.byte772b-771b.balign8.popsection";
    return x86_linux_compact_asm_equals(text, length, expected) ||
           x86_linux_compact_asm_equals(text, length, numeric);
}

/* Text-poking selftest: retain an empty baseline (NOP padded to replacement
 * width) and a relocatable LEA+CALL in the alternative replacement section. */
bool minic_x86_64_linux_alt_reloc_selftest_form(const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(3*32+21)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:lea%0,%%rdi;call__alt_reloc_selftest;"
        "6651:.popsection";
    static const char named[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(3*32+21)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:lea%[mem],%%rdi;call__alt_reloc_selftest;"
        "6651:.popsection";
    return x86_linux_compact_asm_equals(text, length, expected) ||
           x86_linux_compact_asm_equals(text, length, named);
}

static bool x86_linux_alt_reloc_selftest_source(const MinicC0Program *program,
                                                 const MinicInlineAsm *src,
                                                 MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    if (!src || !program || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_alt_reloc_selftest_form(src->template_text,
                                                     src->template_length) ||
        src->output_count || src->input_count != 1U || !src->inputs ||
        src->label_count || src->clobber_count != 1U ||
        src->has_memory_clobber || src->register_clobber_count != 1U ||
        !clobber_is(&src->register_clobbers[0], "rdi") ||
        !target_constraint_is(&src->inputs[0], "m")) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!value || value->value_category != MINIC_VALUE_LVALUE) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* machine_real_restart transfers out of long mode through the real-mode
 * trampoline's six-byte far pointer. The GNU D constraint is the boot
 * restart type: keeping it physically in EDI is part of the ABI. */
bool minic_x86_64_linux_real_mode_far_jump_form(const char *text,
                                                size_t length) {
    return x86_linux_compact_asm_equals(text, length, "ljmpl*%0");
}

/* Linux annotate_unreachable() emits a single reference to the current
 * numeric label in .discard.unreachable. Verify the complete section and
 * label relation; do not allow arbitrary multi-instruction opaque text. */
bool minic_x86_64_linux_unreachable_opaque(const char *text, size_t length) {
    char compact[256], expected[256];
    size_t i, n = 0U, digits = 0U;
    unsigned int label = 0U;
    if (!text || !length || length >= sizeof(compact) || strlen(text) != length)
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    while (digits < n && compact[digits] >= '0' && compact[digits] <= '9') {
        if (digits >= 4U) return false;
        label = label * 10U + (unsigned int)(compact[digits] - '0');
        ++digits;
    }
    if (!digits || !label || compact[digits] != ':') return false;
    return snprintf(expected, sizeof(expected),
        "%u:.pushsection.discard.unreachable.long%ub-..popsection",
        label, label) > 0 && strcmp(compact, expected) == 0;
}

static bool x86_linux_real_mode_far_jump_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *destination, *restart_type;
    MinicType type;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_real_mode_far_jump_form(src->template_text,
                                                    src->template_length) ||
        src->output_count || src->input_count != 2U || !src->inputs ||
        src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !target_constraint_is(&src->inputs[0], "m") ||
        !target_constraint_is(&src->inputs[1], "D")) return false;
    destination = minic_c0_program_expression(program, src->inputs[0].expression);
    restart_type = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!destination || destination->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(destination->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_int()) ||
        !restart_type || !minic_type_unqualified(restart_type->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_int())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Read-once volatile MMIO: exact MOVB/MOVW/MOVL/MOVQ width, register result,
 * and an address-backed memory input. The +memory C clobber is optional:
 * Linux's double-underscore raw helpers intentionally omit it. */
unsigned int minic_x86_64_linux_mmio_read_form(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length, "movb%1,%0")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "movw%1,%0")) return 2U;
    if (x86_linux_compact_asm_equals(text, length, "movl%1,%0")) return 4U;
    if (x86_linux_compact_asm_equals(text, length, "movq%1,%0")) return 8U;
    return 0U;
}

unsigned int minic_x86_64_linux_mmio_write_form(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length, "movb%0,%1")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "movw%0,%1")) return 2U;
    if (x86_linux_compact_asm_equals(text, length, "movl%0,%1")) return 4U;
    if (x86_linux_compact_asm_equals(text, length, "movq%0,%1")) return 8U;
    return 0U;
}

bool minic_x86_64_linux_mmio_type_matches(MinicType type, unsigned int width) {
    MinicType plain;
    if (!minic_type_unqualified(type, &plain)) return false;
    switch (width) {
    case 1U: return minic_type_equal(plain, minic_type_unsigned_char());
    case 2U: return minic_type_equal(plain, minic_type_unsigned_short());
    case 4U: return minic_type_equal(plain, minic_type_unsigned_int());
    case 8U: return minic_type_equal(plain, minic_type_unsigned_long_long()) ||
                    minic_type_equal(plain, minic_type_unsigned_long());
    default: return false;
    }
}

static bool x86_linux_mmio_write_source(const MinicC0Program *program,
                                        const MinicInlineAsm *src,
                                        MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value, *memory;
    const unsigned int width = src ? minic_x86_64_linux_mmio_write_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || !width || !src->is_volatile || src->is_goto ||
        src->output_count || src->input_count != 2U || !src->inputs ||
        src->label_count || src->register_clobber_count ||
        src->clobber_count != (src->has_memory_clobber ? 1U : 0U) ||
        !target_constraint_is(&src->inputs[0], width == 1U ? "q" : "r") ||
        !target_constraint_is(&src->inputs[1], "m")) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!value || !memory || memory->value_category != MINIC_VALUE_LVALUE ||
        !minic_x86_64_linux_mmio_type_matches(value->type, width) ||
        !minic_x86_64_linux_mmio_type_matches(memory->type, width)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_mmio_read_source(const MinicC0Program *program,
                                       const MinicInlineAsm *src,
                                       MinicTargetInlineAsmContract *contract) {
    const MinicExpression *output, *memory;
    const unsigned int width = src ? minic_x86_64_linux_mmio_read_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || !width || !src->is_volatile || src->is_goto ||
        src->output_count != 1U || src->input_count != 1U || !src->outputs ||
        !src->inputs || src->label_count || src->register_clobber_count ||
        src->clobber_count != (src->has_memory_clobber ? 1U : 0U) ||
        !target_constraint_is(&src->outputs[0], width == 1U ? "=q" : "=r") ||
        !target_constraint_is(&src->inputs[0], "m")) return false;
    output = minic_c0_program_expression(program, src->outputs[0].expression);
    memory = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!output || !memory || output->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        !minic_x86_64_linux_mmio_type_matches(output->type, width) ||
        !minic_x86_64_linux_mmio_type_matches(memory->type, width)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* GNU Linux inline `rep; outs{b,w,l}` / `rep; ins{b,w,l}` has two
 * read/write C lvalues (+S/+D address and +c count), and a DX input port.
 * Matching only the exact instruction and register constraints is crucial:
 * a source-level read/write I/O operation may never become an empty asm. */
unsigned int minic_x86_64_linux_rep_string_io_form(const char *text,
                                                     size_t length) {
    if (x86_linux_compact_asm_equals(text, length, "rep;outsb")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "rep;outsw")) return 2U;
    if (x86_linux_compact_asm_equals(text, length, "rep;outsl")) return 3U;
    if (x86_linux_compact_asm_equals(text, length, "rep;insb")) return 4U;
    if (x86_linux_compact_asm_equals(text, length, "rep;insw")) return 5U;
    if (x86_linux_compact_asm_equals(text, length, "rep;insl")) return 6U;
    return 0U;
}

static bool x86_linux_rep_string_io_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *address, *count, *port;
    const unsigned int form = src ? minic_x86_64_linux_rep_string_io_form(
        src->template_text, src->template_length) : 0U;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->output_count != 2U || src->input_count != 1U ||
        !src->outputs || !src->inputs || src->label_count ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], form <= 3U ? "+S" : "+D") ||
        !target_constraint_is(&src->outputs[1], "+c") ||
        !target_constraint_is(&src->inputs[0], "d")) return false;
    address = minic_c0_program_expression(program, src->outputs[0].expression);
    count = minic_c0_program_expression(program, src->outputs[1].expression);
    port = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!address || !count || !port ||
        address->value_category != MINIC_VALUE_LVALUE ||
        count->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(address->type) ||
        !minic_type_equal(count->type, minic_type_unsigned_long()) ||
        !minic_type_equal(port->type, minic_type_unsigned_short())) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

unsigned int minic_x86_64_linux_iomem_copy_form(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length, "movsb")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "movsw")) return 2U;
    if (x86_linux_compact_asm_equals(text, length,
        "rep;movsltestb$2,%b4je1fmovsw1:testb$1,%b4je2fmovsb2:")) return 3U;
    return 0U;
}

static bool x86_linux_iomem_copy_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? minic_x86_64_linux_iomem_copy_form(
        src->template_text, src->template_length) : 0U;
    static const char *const rep_outputs[] = {"=&c", "=&D", "=&S"};
    static const char *const rep_inputs[] = {"0", "q", "1", "2"};
    static const char *const one_outputs[] = {"=&D", "=&S"};
    static const char *const one_inputs[] = {"0", "1"};
    size_t i, outputs, inputs;

    if (!program || !src || !contract || !form || !src->is_volatile || src->is_goto ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count || src->label_count || !src->outputs || !src->inputs)
        return false;
    outputs = form == 3U ? 3U : 2U;
    inputs = form == 3U ? 4U : 2U;
    if (src->output_count != outputs || src->input_count != inputs) return false;
    for (i = 0U; i < outputs; ++i) {
        const MinicInlineAsmOperand *op = &src->outputs[i];
        const MinicExpression *value = minic_c0_program_expression(program, op->expression);
        if (!target_constraint_is(op, form == 3U ? rep_outputs[i] : one_outputs[i]) ||
            !value || value->value_category != MINIC_VALUE_LVALUE ||
            (form == 3U ? !minic_type_is_integer(value->type) :
                          !minic_type_is_pointer(value->type))) return false;
    }
    for (i = 0U; i < inputs; ++i) {
        const MinicInlineAsmOperand *op = &src->inputs[i];
        const MinicExpression *value = minic_c0_program_expression(program, op->expression);
        if (!target_constraint_is(op, form == 3U ? rep_inputs[i] : one_inputs[i]) ||
            !value || (!minic_type_is_integer(value->type) &&
                       !minic_type_is_pointer(value->type))) return false;
    }
    contract->operand_count = outputs + inputs;
    for (i = 0U; i < contract->operand_count; ++i)
        contract->kinds[i] = i < outputs
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static uint64_t x86_linux_template_fingerprint(const char *text, size_t length) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0U; i < length; ++i) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

unsigned int minic_x86_64_linux_flushcache_copy_form(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length,
        "movq(%0),%%r8movq8(%0),%%r9movq16(%0),%%r10movq24(%0),%%r11"
        "movnti%%r8,(%1)movnti%%r9,8(%1)movnti%%r10,16(%1)movnti%%r11,24(%1)"))
        return 1U;
    if (x86_linux_compact_asm_equals(text, length,
        "movq(%0),%%r8movnti%%r8,(%1)")) return 2U;
    if (x86_linux_compact_asm_equals(text, length,
        "movl(%0),%%r8dmovnti%%r8d,(%1)")) return 3U;
    /* The source names its memory/pointer operands; Core consistently
       rewrites %[pax] to %1, shortening the otherwise identical template.
       Accept both exact source and normalized alternatives, never a partial
       prefix that might lose one of the two feature records. */
    if (text && length == 704U &&
        x86_linux_template_fingerprint(text, length) == UINT64_C(0xd4e9ddad388cfc52) &&
        strstr(text, "# ALT: oldinstr2") == text &&
        strstr(text, ".altinstructions") != NULL &&
        strstr(text, ".altinstr_replacement") != NULL &&
        strstr(text, "0x66, 0x0f, 0xae, 0x30") != NULL)
        return 4U;
    /* The 712-byte Linux `clwb` alternative preserves both CPU-feature
       replacement records; using a 64-bit full-text fingerprint plus the
       exact documented section/instruction sentinels avoids classifying an
       arbitrary inline template as privileged cache-maintenance ASM. */
    if (text && length == 712U &&
        x86_linux_template_fingerprint(text, length) == UINT64_C(0xef6513d224b5c286) &&
        strstr(text, "# ALT: oldinstr2") == text &&
        strstr(text, ".altinstructions") != NULL &&
        strstr(text, ".altinstr_replacement") != NULL &&
        strstr(text, "0x66, 0x0f, 0xae, 0x30") != NULL)
        return 4U;
    return 0U;
}

static bool x86_linux_flushcache_copy_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? minic_x86_64_linux_flushcache_copy_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *expr;
    size_t i;
    if (!program || !src || !contract || !form || src->is_goto ||
        src->label_count || src->output_count != (form == 4U ? 1U : 0U) ||
        src->input_count != (form == 4U ? 1U : 2U) || !src->inputs ||
        (form == 4U && !src->outputs) ||
        src->has_memory_clobber != (form != 4U) ||
        src->clobber_count != (form == 4U ? 0U : (form == 1U ? 5U : 2U)) ||
        src->register_clobber_count != (form == 4U ? 0U : (form == 1U ? 4U : 1U)) ||
        src->is_volatile != (form == 4U)) return false;
    if (form == 4U) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (!target_constraint_is(&src->outputs[0], "+m") || !expr ||
            expr->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_record(expr->type) ||
            !target_constraint_is(&src->inputs[0], "a")) return false;
    }
    for (i = 0U; i < src->input_count; ++i) {
        expr = minic_c0_program_expression(program, src->inputs[i].expression);
        if (!target_constraint_is(&src->inputs[i], form == 4U ? "a" : "r") ||
            !expr || (!minic_type_is_pointer(expr->type) &&
                      !minic_type_is_integer(expr->type)))
            return false;
    }
    if (form != 4U) {
        static const char *const clobbers[] = {"r8", "r9", "r10", "r11"};
        for (i = 0U; i < src->register_clobber_count; ++i)
            if (!clobber_is(&src->register_clobbers[i], clobbers[i])) return false;
    }
    contract->operand_count = src->input_count + src->output_count;
    for (i = 0U; i < contract->operand_count; ++i)
        contract->kinds[i] = form == 4U && i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

unsigned int minic_x86_64_linux_atomic_xchg_form(const char *s, size_t len) {
    if (x86_linux_compact_asm_equals(s, len, "xchgb%b0,%1")) return 1U;
    if (x86_linux_compact_asm_equals(s, len, "xchgw%w0,%1")) return 2U;
    if (x86_linux_compact_asm_equals(s, len, "xchgl%0,%1")) return 4U;
    if (x86_linux_compact_asm_equals(s, len, "xchgq%q0,%1")) return 8U;
    return 0U;
}

static bool x86_linux_atomic_xchg_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *reg, *mem;
    MinicType reg_type, mem_type;
    const unsigned int width = src ? minic_x86_64_linux_atomic_xchg_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || !width || !src->is_volatile ||
        src->is_goto || src->output_count != 2U || src->input_count ||
        !src->outputs || src->label_count || !src->has_memory_clobber ||
        src->clobber_count != 2U || src->register_clobber_count != 1U ||
        !clobber_is(&src->register_clobbers[0], "cc") ||
        !target_constraint_is(&src->outputs[0], width == 1U ? "+q" : "+r") ||
        !target_constraint_is(&src->outputs[1], "+m")) return false;
    reg = minic_c0_program_expression(program, src->outputs[0].expression);
    mem = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!reg || !mem || reg->value_category != MINIC_VALUE_LVALUE ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(reg->type, &reg_type) ||
        !minic_type_unqualified(mem->type, &mem_type) ||
        !minic_type_equal(reg_type, mem_type) ||
        !(minic_type_is_integer(reg_type) || minic_type_is_pointer(reg_type)))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux pvclock's 64x64->128 multiplication and 32-bit right funnel shift.
 * The RDX:RAX pair is an ABI contract, not a plain 64-bit product. */
unsigned int minic_x86_64_linux_pvclock_scale_form(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(
            text, length, "mulq%[mul_frac];shrd$32,%[hi],%[lo]") ||
        x86_linux_compact_asm_equals(text, length, "mulq%3;shrd$32,%1,%0"))
        return 1U;
    return 0U;
}

static bool x86_linux_pvclock_scale_source(const MinicC0Program *program,
                                            const MinicInlineAsm *src,
                                            MinicTargetInlineAsmContract *contract) {
    static const char *const output_constraints[] = {"=a", "=d"};
    const MinicExpression *expressions[4];
    size_t i;
    if (!src || !program || !contract ||
        !minic_x86_64_linux_pvclock_scale_form(src->template_text, src->template_length) ||
        src->is_volatile || src->is_goto || src->output_count != 2U ||
        src->input_count != 2U || !src->outputs || !src->inputs ||
        src->label_count || src->clobber_count || src->has_memory_clobber ||
        src->register_clobber_count ||
        !target_constraint_is(&src->inputs[0], "0") ||
        !target_constraint_is(&src->inputs[1], "rm")) return false;
    for (i = 0U; i < 2U; ++i) {
        if (!target_constraint_is(&src->outputs[i], output_constraints[i])) return false;
        expressions[i] = minic_c0_program_expression(program, src->outputs[i].expression);
        expressions[i + 2U] = minic_c0_program_expression(program, src->inputs[i].expression);
    }
    for (i = 0U; i < 4U; ++i)
        if (!expressions[i] || !minic_type_is_integer(expressions[i]->type) ||
            (i < 2U && expressions[i]->value_category != MINIC_VALUE_LVALUE)) return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_linux_pv_indirect_call_source(const MinicC0Program *program,
                                               const MinicInlineAsm *src,
                                               MinicTargetInlineAsmContract *contract) {
    static const char *const outputs[] = {"=D", "=S", "=d", "=c", "+r"};
    static const char *const clobbers[] = {"cc", "rax", "r8", "r9", "r10", "r11"};
    const MinicExpression *address, *type_number, *stack;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_pv_indirect_call_form(src->template_text,
                                                  src->template_length) ||
        src->output_count != 5U || !src->outputs || src->input_count != 2U ||
        !src->inputs || src->label_count || !src->has_memory_clobber ||
        src->clobber_count != 7U || src->register_clobber_count != 6U ||
        !src->register_clobbers ||
        !target_constraint_is(&src->inputs[0], "i") ||
        !target_constraint_is(&src->inputs[1], "m")) return false;
    for (i = 0U; i < 5U; ++i)
        if (!target_constraint_is(&src->outputs[i], outputs[i])) return false;
    for (i = 0U; i < 6U; ++i)
        if (!clobber_is(&src->register_clobbers[i], clobbers[i])) return false;
    stack = minic_c0_program_expression(program, src->outputs[4].expression);
    type_number = minic_c0_program_expression(program, src->inputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[1].expression);
    if (!stack || !type_number || !address ||
        !minic_type_equal(stack->type, minic_type_unsigned_long()) ||
        !minic_type_is_integer(type_number->type) ||
        address->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(address->type)) return false;
    contract->operand_count = 7U;
    for (i = 0U; i < 4U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[5] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[6] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_task_control_source(const MinicC0Program *program,
                                           const MinicInlineAsm *src,
                                           MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    const unsigned int form = src ? minic_x86_64_linux_task_control_form(
        src->template_text, src->template_length) : 0U;
    if (!src || !program || !contract || src->is_goto || !src->is_volatile ||
        src->clobber_count || src->label_count || src->has_memory_clobber ||
        src->register_clobber_count || !form ||
        (form == 1U
            ? (src->output_count != 0U || src->input_count != 1U ||
               !src->inputs || !target_constraint_is(&src->inputs[0], "q"))
            : (src->output_count != 1U || src->input_count != 0U ||
               !src->outputs || !target_constraint_is(&src->outputs[0], "=r"))))
        return false;
    expr = minic_c0_program_expression(program,
        form == 1U ? src->inputs[0].expression : src->outputs[0].expression);
    if (!expr || !minic_type_is_integer(expr->type) ||
        (form == 2U && (expr->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expr->type)))) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form == 1U
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool x86_linux_iret_to_self_source(const MinicC0Program *program,
                                          const MinicInlineAsm *src,
                                          MinicTargetInlineAsmContract *contract) {
    const MinicExpression *tmp, *stack;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_iret_to_self_form(src->template_text, src->template_length) ||
        src->output_count != 2U || !src->outputs || src->input_count ||
        !src->has_memory_clobber || src->clobber_count != 2U ||
        src->register_clobber_count != 1U ||
        !clobber_is(&src->register_clobbers[0], "cc") || src->label_count ||
        !target_constraint_is(&src->outputs[0], "=&r") ||
        !target_constraint_is(&src->outputs[1], "+r")) return false;
    tmp = minic_c0_program_expression(program, src->outputs[0].expression);
    stack = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!tmp || !stack || tmp->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(tmp->type, minic_type_unsigned_int()) ||
        !minic_type_equal(stack->type, minic_type_unsigned_long())) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_int3_selftest_source(const MinicC0Program *program,
                                            const MinicInlineAsm *src,
                                            MinicTargetInlineAsmContract *contract) {
    const MinicExpression *stack, *address;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_int3_selftest_form(src->template_text, src->template_length) ||
        src->output_count != 1U || !src->outputs || src->input_count != 1U ||
        !src->inputs || src->label_count || !src->has_memory_clobber ||
        src->clobber_count != 1U || src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "+r") ||
        !target_constraint_is(&src->inputs[0], "D")) return false;
    stack = minic_c0_program_expression(program, src->outputs[0].expression);
    address = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!stack || !address ||
        !minic_type_equal(stack->type, minic_type_unsigned_long()) ||
        !minic_type_is_pointer(address->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_lidt_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *descriptor;
    const MinicRecord *record;
    MinicType unqualified;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_descriptor_table_form(
            src->template_text, src->template_length) ||
        (src->output_count != 0U && src->input_count != 0U) ||
        (src->output_count + src->input_count != 1U) ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        (minic_x86_64_linux_descriptor_table_form(
             src->template_text, src->template_length) >= 3U
             ? (!src->outputs || !target_constraint_is(&src->outputs[0], "=m"))
             : (!src->inputs || !target_constraint_is(&src->inputs[0], "m")))) return false;
    descriptor = minic_c0_program_expression(
        program, src->input_count ? src->inputs[0].expression : src->outputs[0].expression);
    if (!descriptor || descriptor->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(descriptor->type, &unqualified) ||
        !minic_type_is_record(unqualified)) return false;
    record = minic_c0_program_record(program, unqualified.record_id);
    if (!record || !record->is_complete || record->is_union ||
        !record->is_packed || record->field_count != 2U ||
        !record->fields || record->fields[0].is_bit_field ||
        record->fields[1].is_bit_field ||
        !record->fields[0].name || !record->fields[1].name ||
        strcmp(record->fields[0].name, "size") != 0 ||
        strcmp(record->fields[1].name, "address") != 0 ||
        !minic_type_equal(record->fields[0].type, minic_type_unsigned_short()) ||
        !minic_type_equal(record->fields[1].type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = src->output_count
                             ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
                             : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux head64 startup_gdt: reload DS/SS/ES with the exact kernel data
 * selector in AX. These are privileged CPU segment-register writes, not
 * ordinary stores. The GNU source uses an `a` input and requires memory
 * sequencing; the machine owner chooses the real physical EAX/AX register. */
bool minic_x86_64_linux_boot_segments_template(
    const char *text, size_t length) {
    static const char expected[] =
        "movl%%eax,%%dsmovl%%eax,%%ssmovl%%eax,%%es";
    char compact[120];
    size_t n = 0U, i;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* Linux interrupt flag, halted-CPU state and cache WB/INV transitions. These
 * exact
 * zero-operand GNU templates are privileged machine instructions; their
 * compiler memory sequencing is essential, and the Target must not
 * reinterpret them as ordinary optimizer barriers or omit the instruction. */
unsigned int minic_x86_64_linux_privileged_zero_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "cli", "sti", "sti; hlt", "hlt", "wbinvd", "swapgs",
        ".byte 0xf, 0x1, 0xe8", "outb %al, $0x80", "outb %al, $0xed",
        "int3"
    };
    size_t i;
    if (!text || length == 0U || length > 32U) return 0U;
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (length == strlen(forms[i]) &&
            memcmp(text, forms[i], length) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
}

/* TILERELEASE is an architectural AMX tile-register state transition. Linux
 * spells it as raw VEX bytes to support assemblers without the mnemonic;
 * only this exact zero-operand encoding is eligible for native emission. */
bool minic_x86_64_linux_amx_tilerelease_form(const char *text, size_t length) {
    static const char bytes[] = ".byte0xc4,0xe2,0x78,0x49,0xc0";
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    return strcmp(compact, bytes) == 0;
}

/* `fpu__drop` waits for any pending x87 exception and installs a type-1
 * exception-table recovery entry around FWAIT. The fixup metadata is part
 * of the instruction contract, not a removable assembler annotation. */
bool minic_x86_64_linux_x87_fwait_fault_form(const char *text, size_t length) {
    static const char expected[] =
        "1:fwait2:.pushsection\"__ex_table\",\"a\""
        ".balign4.long(1b)-..long(2b)-..long1.popsection";
    char compact[192];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* WRMSR ordering fence with Linux's feature-flagged empty replacement.
 * The two fences and complete .altinstructions record are inseparable: a
 * bare MFENCE/LFENCE is not equivalent on CPUs advertising this feature. */
bool minic_x86_64_linux_weak_wrmsr_fence_form(const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:mfence;lfence662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(((1<<0)<<16)|((11*32+27)))"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:6651:.popsection";
    char compact[512];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

static bool x86_linux_boot_segments_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    MinicType type;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_boot_segments_template(
            src->template_text, src->template_length) ||
        src->output_count != 0U || src->input_count != 1U || !src->inputs ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || src->label_count != 0U ||
        !target_constraint_is(&src->inputs[0], "a")) return false;
    value = minic_c0_program_expression(program, src->inputs[0].expression);
    if (!value || !minic_type_unqualified(value->type, &type) ||
        !minic_type_is_integer(type) ||
        (type.integer_rank != MINIC_INTEGER_RANK_INT &&
         type.integer_rank != MINIC_INTEGER_RANK_SHORT &&
         type.integer_rank != MINIC_INTEGER_RANK_LONG)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* This is the complete Linux three-way CALL alternatives grammar, including
 * every relocation and feature number. The C preprocessor expands a lengthy
 * MAX() expression inside .skip; compare the full expression, not a prefix,
 * and never admit unrecognized assembler input through the Core boundary.
 * Named input spelling is normalized to its numeric Core position later. */
#define X86_ALT_CALL_MAX \
    "((6651f-6641f)^(((6651f-6641f)^(6652f-6642f))&-(-((6651f-6641f)<(6652f-6642f)))))"
#define X86_ALT_CALL_SKIP \
    ".skip-((" X86_ALT_CALL_MAX "-(662b-661b))>0)*(" \
    X86_ALT_CALL_MAX "-(662b-661b)),0x90"

bool minic_x86_64_linux_three_call_alternative_form(
    const char *text, size_t length) {
    static const char *const lines[] = {
        "#ALT:oldinstr2", "661:", NULL, "662:",
        "#ALT:padding2", X86_ALT_CALL_SKIP, "663:",
        ".pushsection.altinstructions,\"a\"",
        ".long661b-.", ".long6641f-.", ".4byte(3*32+16)",
        ".byte663b-661b", ".byte6651f-6641f",
        ".long661b-.", ".long6642f-.", ".4byte(9*32+9)",
        ".byte663b-661b", ".byte6652f-6642f",
        ".popsection", ".pushsection.altinstr_replacement,\"ax\"",
        "#ALT:replacement1", "6641:", NULL, "6651:",
        "#ALT:replacement2", "6642:", NULL, "6652:",
        ".popsection"
    };
    const size_t count = sizeof(lines) / sizeof(lines[0]);
    size_t position = 0U, line_index = 0U;
    if (!text || length < 250U || length > 1600U ||
        strlen(text) != length) return false;
    while (position < length) {
        char compact[1000];
        size_t n = 0U;
        while (position < length && text[position] != '\n') {
            const char c = text[position++];
            if (c == ' ' || c == '\t' || c == '\r') continue;
            if (c == '\0' || n + 1U >= sizeof(compact)) return false;
            compact[n++] = c;
        }
        if (position < length) ++position;
        compact[n] = '\0';
        if (n == 0U) continue;
        if (line_index >= count) return false;
        if (lines[line_index]) {
            if (strcmp(compact, lines[line_index]) != 0) return false;
        } else if (line_index == 2U) {
            if (strcmp(compact, "call%c[old]") != 0 &&
                strcmp(compact, "call%c2") != 0) return false;
        } else if (line_index == 22U) {
            if (strcmp(compact, "call%c[new1]") != 0 &&
                strcmp(compact, "call%c3") != 0) return false;
        } else if (line_index == 26U) {
            if (strcmp(compact, "call%c[new2]") != 0 &&
                strcmp(compact, "call%c4") != 0) return false;
        } else return false;
        ++line_index;
    }
    return line_index == count;
}
#undef X86_ALT_CALL_SKIP
#undef X86_ALT_CALL_MAX

/* Linux _static_cpu_has: exactly two C goto targets, three immediate
 * operands, an 8-bit bit test in the auxiliary text section, and two runtime
 * alternative entries. A token-by-token grammar rejects even one extra
 * instruction, change of CPU feature metadata or section-order mutation. */
#define X86_CPU_HAS_MAX \
    "((6651f-6641f)^(((6651f-6641f)^(6652f-6642f))&-(-((6651f-6641f)<(6652f-6642f)))))"
#define X86_CPU_HAS_SKIP \
    ".skip-((" X86_CPU_HAS_MAX "-(662b-661b))>0)*(" \
    X86_CPU_HAS_MAX "-(662b-661b)),0x90"

bool minic_x86_64_linux_cpu_has_goto_form(
    const char *text, size_t length) {
    static const char *const lines[] = {
        "#ALT:oldinstr2", "661:", "jmp6f", "662:",
        "#ALT:padding2", X86_CPU_HAS_SKIP, "663:",
        ".pushsection.altinstructions,\"a\"",
        ".long661b-.", ".long6641f-.", ".4byte(3*32+21)",
        ".byte663b-661b", ".byte6651f-6641f",
        ".long661b-.", ".long6642f-.", ".4byte%c[feature]",
        ".byte663b-661b", ".byte6652f-6642f",
        ".popsection", ".pushsection.altinstr_replacement,\"ax\"",
        "#ALT:replacement1", "6641:", "jmp%l[t_no]", "6651:",
        "#ALT:replacement2", "6642:", "6652:", ".popsection",
        ".pushsection.altinstr_aux,\"ax\"", "6:",
        "testb%[bitnum],%P[cap_byte](%%rip)",
        "jnz%l[t_yes]", "jmp%l[t_no]", ".popsection"
    };
    const size_t count = sizeof(lines) / sizeof(lines[0]);
    size_t cursor = 0U, line_index = 0U;
    if (!text || length < 600U || length > 1100U || strlen(text) != length)
        return false;
    while (cursor < length) {
        char compact[500];
        size_t n = 0U;
        while (cursor < length && text[cursor] != '\n') {
            const char ch = text[cursor++];
            if (ch == ' ' || ch == '\t' || ch == '\r') continue;
            if (ch == '\0' || n + 1U >= sizeof(compact)) return false;
            compact[n++] = ch;
        }
        if (cursor < length) ++cursor;
        compact[n] = '\0';
        if (n == 0U) continue;
        if (line_index >= count || strcmp(compact, lines[line_index]) != 0)
            return false;
        ++line_index;
    }
    return line_index == count;
}
#undef X86_CPU_HAS_SKIP
#undef X86_CPU_HAS_MAX

static bool x86_linux_jump_label_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract);

bool minic_x86_64_qualify_inline_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source) {
    static const char *const names[] = {"feature", "bitnum", "cap_byte"};
    static const char *const labels[] = {"t_yes", "t_no"};
    const MinicExpression *expression;
    MinicTargetInlineAsmContract contract;
    size_t i;
    /* Both target-qualified shapes must pass through the one Core symbolic
     * immediate resolver; it is not valid for the static-key pointer to be
     * materialized as a runtime register in StructuredInlineAsm. */
    (void)memset(&contract, 0, sizeof(contract));
    if (x86_linux_jump_label_source(program, source, &contract)) return true;
    if (!program || !source || !source->is_goto || source->is_volatile ||
        !minic_x86_64_linux_cpu_has_goto_form(
            source->template_text, source->template_length) ||
        source->output_count != 0U || source->input_count != 3U ||
        !source->inputs || source->label_count != 2U || !source->labels ||
        source->clobber_count != 0U || source->register_clobber_count != 0U ||
        source->has_memory_clobber) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *input = &source->inputs[i];
        if (!target_constraint_is(input, "i") ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !input->name || input->name_length != strlen(names[i]) ||
            memcmp(input->name, names[i], input->name_length) != 0)
            return false;
        expression = minic_c0_program_expression(program, input->expression);
        if (!expression || (i == 2U
            ? !minic_type_is_pointer(expression->type)
            : !minic_type_is_integer(expression->type))) return false;
    }
    for (i = 0U; i < 2U; ++i) {
        const MinicInlineAsmLabel *label = &source->labels[i];
        const MinicStatement *target_statement;
        if (!label->name || label->name_length != strlen(labels[i]) ||
            memcmp(label->name, labels[i], label->name_length) != 0 ||
            label->target_statement == MINIC_STATEMENT_INVALID) return false;
        target_statement = minic_c0_program_statement(
            program, label->target_statement);
        if (!target_statement || target_statement->kind != MINIC_STATEMENT_LABEL)
            return false;
    }
    return true;
}

static bool x86_linux_three_call_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract);

/* x86 irq_stack.h call_on_stack(): two original GNU layouts and their
 * Core-numeric equivalents. The mandatory .discard.reachable entry is
 * matched as part of the call contract so objtool's CFG metadata survives. */
unsigned int minic_x86_64_linux_irq_stack_call_form(
    const char *text, size_t length) {
    static const char *const named[] = {
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "callirq_enter_rcumovq%[arg1],%%rdi"
        "call%c[__func]998:.pushsection.discard.reachable"
        ".long998b.popsectioncallirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "callirq_enter_rcumovq%[arg2],%%rsi"
        "movq%[arg1],%%rdicall%c[__func]998:"
        ".pushsection.discard.reachable.long998b.popsection"
        "callirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%[tos])movq%[tos],%%rsp"
        "call%c[__func]998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp"
    };
    static const char *const numeric[] = {
        "movq%%rsp,(%3)movq%3,%%rsp"
        "callirq_enter_rcumovq%4,%%rdi"
        "call%c2" "998:.pushsection.discard.reachable"
        ".long998b.popsectioncallirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%3)movq%3,%%rsp"
        "callirq_enter_rcumovq%5,%%rsi"
        "movq%4,%%rdicall%c2" "998:"
        ".pushsection.discard.reachable.long998b.popsection"
        "callirq_exit_rcupopq%%rsp",
        "movq%%rsp,(%3)movq%3,%%rsp"
        "call%c2" "998:.pushsection.discard.reachable"
        ".long998b.popsectionpopq%%rsp"
    };
    char compact[512];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 3U; ++i)
        if (strcmp(compact, named[i]) == 0 ||
            strcmp(compact, numeric[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_irq_stack_call_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    static const char *const expected_clobbers[] = {
        "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10"
    };
    const MinicExpression *tos, *stack, *callee, *input, *regs;
    const MinicFixedRegisterBinding *binding;
    MinicType type;
    const unsigned int form = src ? minic_x86_64_linux_irq_stack_call_form(
        src->template_text, src->template_length) : 0U;
    size_t i;
    if (!program || !src || !contract || form == 0U ||
        !src->is_volatile || src->is_goto ||
        src->output_count != 2U || !src->outputs ||
        src->input_count != (form == 1U ? 3U : form == 2U ? 4U : 2U) ||
        !src->inputs ||
        src->label_count || !src->has_memory_clobber ||
        src->register_clobber_count != 9U || !src->register_clobbers ||
        src->clobber_count != 10U ||
        !target_constraint_is(&src->outputs[0], "+r") ||
        !target_constraint_is(&src->outputs[1], "+r") ||
        !target_constraint_is(&src->inputs[0], "i")) return false;
    for (i = 1U; i < src->input_count; ++i)
        if (!target_constraint_is(&src->inputs[i], "r")) return false;
    for (i = 0U; i < 9U; ++i)
        if (!clobber_is(&src->register_clobbers[i], expected_clobbers[i])) {
            return false;
        }
    tos = minic_c0_program_expression(program, src->outputs[0].expression);
    stack = minic_c0_program_expression(program, src->outputs[1].expression);
    callee = minic_c0_program_expression(program, src->inputs[0].expression);
    input = minic_c0_program_expression(program, src->inputs[1].expression);
    regs = form == 3U ? NULL : minic_c0_program_expression(
        program, src->inputs[2].expression);
    if (!tos || !stack || !callee || !input || (form != 3U && !regs) ||
        tos->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(tos->type) ||
        !minic_type_unqualified(stack->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()) ||
        callee->kind != MINIC_EXPRESSION_FUNCTION ||
        !minic_type_is_pointer(callee->type) ||
        !minic_type_equal(tos->type, input->type) ||
        (form != 3U && !minic_type_is_pointer(regs->type))) return false;
    binding = stack->kind == MINIC_EXPRESSION_FIXED_REGISTER ?
        minic_c0_program_fixed_register_binding(
            program, stack->value.fixed_register_binding_id) :
        stack->kind == MINIC_EXPRESSION_LOCAL ?
        minic_c0_program_local_fixed_register_binding(
            program, stack->value.local_id) : NULL;
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "rsp") != 0) return false;
    binding = tos->kind == MINIC_EXPRESSION_FIXED_REGISTER ?
        minic_c0_program_fixed_register_binding(
            program, tos->value.fixed_register_binding_id) :
        tos->kind == MINIC_EXPRESSION_LOCAL ?
        minic_c0_program_local_fixed_register_binding(
            program, tos->value.local_id) : NULL;
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "r11") != 0) return false;
    if (form == 2U) {
        input = minic_c0_program_expression(program, src->inputs[3].expression);
        if (!input || !minic_type_is_integer(input->type)) return false;
    }
    contract->operand_count = 2U + src->input_count;
    /* ASM never writes the R11 `tos` value and restores RSP before exit.
     * GNU's +r modes are register-liveness contracts, not C stores. Keep
     * both in their live SSA form so address-of a register local is avoided. */
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    for (i = 2U; i < contract->operand_count; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_three_call_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    const MinicExpression *address;
    const MinicFixedRegisterBinding *fixed;
    size_t i;
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_three_call_alternative_form(
            src->template_text, src->template_length) ||
        src->output_count != 2U || src->input_count != 4U ||
        !src->outputs || !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 4U ||
        src->register_clobber_count != 3U || !src->register_clobbers ||
        !target_constraint_is(&src->outputs[0], "=D") ||
        !target_constraint_is(&src->outputs[1], "+r") ||
        !target_constraint_is(&src->inputs[0], "i") ||
        !target_constraint_is(&src->inputs[1], "i") ||
        !target_constraint_is(&src->inputs[2], "i") ||
        !target_constraint_is(&src->inputs[3], "0") ||
        !clobber_is(&src->register_clobbers[0], "cc") ||
        !clobber_is(&src->register_clobbers[1], "rax") ||
        !clobber_is(&src->register_clobbers[2], "rcx")) return false;
    value = minic_c0_program_expression(program, src->outputs[0].expression);
    address = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!value || value->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(value->type) ||
        !address ||
        !minic_type_is_integer(address->type)) return false;
    /* +r on current_stack_pointer must truly bind RSP, never a freely
     * allocated general register or synthesized read/write C temporary. */
    if (address->kind != MINIC_EXPRESSION_FIXED_REGISTER) return false;
    fixed = minic_c0_program_fixed_register_binding(
        program, address->value.fixed_register_binding_id);
    if (!fixed || !fixed->register_name ||
        strcmp(fixed->register_name, "rsp") != 0) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *fn = minic_c0_program_expression(
            program, src->inputs[i].expression);
        if (!fn || fn->kind != MINIC_EXPRESSION_FUNCTION ||
            !minic_type_is_pointer(fn->type)) return false;
    }
    value = minic_c0_program_expression(program, src->inputs[3].expression);
    if (!value || !minic_type_is_pointer(value->type)) return false;
    contract->operand_count = 6U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    /* A CALL restores RSP before the instruction ends. Preserve its fixed
     * identity as an input Core value: there is no addressable C lvalue for
     * a hardware stack pointer and no post-asm register write to commit. */
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    for (i = 2U; i < 6U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* A read of CR0/CR2/CR3/CR4 is not a C memory load; the dummy GNU `m`
 * operand exists only to constrain reordering of the privileged CPU read.
 * Match both instruction and operand footprint, then preserve that logical
 * dependency as a Core memory effect without dereferencing its fake address.
 * Writes of CR2/CR3 are physical machine state updates and a full compiler
 * barrier, not a normal pointer assignment. */
unsigned int minic_x86_64_linux_control_register_form(
    const char *text, size_t length) {
    static const char *const names[] = {
        "mov%%cr0,%0", "mov%%cr2,%0", "mov%%cr3,%0", "mov%%cr4,%0",
        "mov%0,%%cr2", "mov%0,%%cr3", "mov%0,%%cr0", "mov%0,%%cr4"
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch == '\0' || ch == ';' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(compact, names[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_control_register_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_control_register_form(
        source->template_text, source->template_length) : 0U;
    const bool read_cr = form != 0U && form <= 4U;
    const bool plain_read_cr = read_cr && source->input_count == 0U;
    const bool tied_write_control = (form == 7U || form == 8U) &&
        source->output_count == 1U;
    const bool plain_write_control = (form == 7U || form == 8U) &&
        source->output_count == 0U;
    const MinicExpression *value, *fake_memory;
    MinicType plain;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count != 0U ||
        source->register_clobber_count != 0U ||
        source->output_count != (read_cr || tied_write_control ? 1U : 0U) ||
        source->input_count != (tied_write_control || plain_read_cr ? 0U : 1U) ||
        (!tied_write_control && !plain_read_cr && !source->inputs) ||
        ((read_cr || tied_write_control) && !source->outputs) ||
        source->clobber_count != (read_cr || plain_write_control ? 0U : 1U) ||
        source->has_memory_clobber == (read_cr || plain_write_control))
        return false;
    if (read_cr) {
        if (!target_constraint_is(&source->outputs[0], "=r") ||
            (!plain_read_cr &&
             !target_constraint_is(&source->inputs[0], "m"))) return false;
        value = minic_c0_program_expression(
            program, source->outputs[0].expression);
        fake_memory = plain_read_cr ? NULL : minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!value || value->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(value->type) ||
            !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()) ||
            (!plain_read_cr &&
             (!fake_memory || fake_memory->value_category != MINIC_VALUE_LVALUE ||
              !minic_type_unqualified(fake_memory->type, &plain) ||
              !minic_type_equal(plain, minic_type_unsigned_int())))) return false;
        contract->operand_count = plain_read_cr ? 1U : 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (!plain_read_cr)
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    } else if (tied_write_control) {
        if (!target_constraint_is(&source->outputs[0], "+r")) return false;
        value = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!value || value->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(value->type) ||
            !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long())) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    } else {
        if (!target_constraint_is(&source->inputs[0], "r")) return false;
        value = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!value || !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long())) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->implicit_memory_effect = true;
    return true;
}

unsigned int minic_x86_64_linux_tlb_cache_form(const char *text, size_t len) {
    if (!text) return 0U;
    if (len == 11U && memcmp(text, "invlpg (%0)", 11U) == 0) return 1U;
    if (len == 10U && memcmp(text, "clflush %0", 10U) == 0) return 2U;
    return 0U;
}

static bool x86_linux_tlb_cache_source(const MinicC0Program *program,
    const MinicInlineAsm *src, MinicTargetInlineAsmContract *contract) {
    const unsigned int form = src ? minic_x86_64_linux_tlb_cache_form(
        src->template_text, src->template_length) : 0U;
    const MinicExpression *expr;
    MinicType plain;
    if (!program || !src || !contract || !form || !src->is_volatile ||
        src->is_goto || src->label_count != 0U ||
        src->register_clobber_count != 0U) return false;
    if (form == 1U) {
        if (src->output_count != 0U || src->input_count != 1U ||
            !src->inputs || src->clobber_count != 1U ||
            !src->has_memory_clobber ||
            !target_constraint_is(&src->inputs[0], "r")) return false;
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (!expr ||
            (!minic_type_is_pointer(expr->type) &&
             (!minic_type_unqualified(expr->type, &plain) ||
              !minic_type_equal(plain, minic_type_unsigned_long()))))
            return false;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    } else {
        if (src->output_count != 1U || src->input_count != 0U ||
            !src->outputs || src->clobber_count != 0U ||
            src->has_memory_clobber ||
            !target_constraint_is(&src->outputs[0], "+m")) return false;
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expr->type)) return false;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    }
    contract->operand_count = 1U;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux writes MSR values through a static feature alternative. Before the
 * capability is enabled, the baseline is two NOP bytes; after patching, it
 * becomes WRMSR with ECX/EAX/EDX holding the caller's exact operands. This
 * metadata carries an assembler-resolved .skip and the CPU feature bit.
 * Keep a closed line grammar so no arbitrary .pushsection or machine opcode
 * can sneak through merely because a template starts with `# ALT`. */
bool minic_x86_64_linux_alternative_wrmsr_form(
    const char *text, size_t length) {
    static const char *const lines[] = {
        "#ALT:oldnstr", "661:", "662:", "#ALT:padding",
        ".skip-(((6651f-6641f)-(662b-661b))>0)*((6651f-6641f)-(662b-661b)),0x90",
        "663:", ".pushsection.altinstructions,\"a\"",
        ".long661b-.", ".long6641f-.", NULL,
        ".byte663b-661b", ".byte6651f-6641f",
        ".popsection", ".pushsection.altinstr_replacement,\"ax\"",
        "#ALT:replacement1", "6641:", "wrmsr", "6651:", ".popsection"
    };
    size_t cursor = 0U, line_index = 0U;
    const size_t count = sizeof(lines) / sizeof(lines[0]);
    if (!text || length < 190U || length > 600U || strlen(text) != length)
        return false;
    while (cursor < length) {
        char compact[250];
        size_t n = 0U;
        while (cursor < length && text[cursor] != '\n') {
            const char c = text[cursor++];
            if (c == ' ' || c == '\t' || c == '\r') continue;
            if (c == '\0' || n + 1U >= sizeof(compact)) return false;
            compact[n++] = c;
        }
        if (cursor < length) ++cursor;
        compact[n] = '\0';
        if (n == 0U) continue;
        if (line_index >= count) return false;
        if (lines[line_index]) {
            if (strcmp(compact, lines[line_index]) != 0) return false;
        } else if (line_index == 9U) {
            if (strcmp(compact, ".4byte%c[feature]") != 0 &&
                strcmp(compact, ".4byte%c3") != 0) return false;
        } else return false;
        ++line_index;
    }
    return line_index == count;
}

static bool x86_linux_alternative_wrmsr_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    MinicType plain;
    size_t i;
    static const char *const constraints[] = {"c", "a", "d", "i"};
    if (!program || !src || !contract || !src->is_volatile || src->is_goto ||
        !minic_x86_64_linux_alternative_wrmsr_form(
            src->template_text, src->template_length) ||
        src->output_count != 0U || src->input_count != 4U ||
        !src->inputs || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U) return false;
    for (i = 0U; i < 4U; ++i) {
        if (!target_constraint_is(&src->inputs[i], constraints[i]))
            return false;
        value = minic_c0_program_expression(program, src->inputs[i].expression);
        if (!value || !minic_type_unqualified(value->type, &plain) ||
            !minic_type_is_integer(plain)) return false;
        if (i == 0U && !minic_type_equal(plain, minic_type_unsigned_int()))
            return false;
        if ((i == 1U || i == 2U) &&
            !minic_type_equal(plain, minic_type_unsigned_int())) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    return true;
}

/* Exact original Linux cmp/sbb two-instruction target ISA shape. */
bool minic_x86_64_asm_nospec_template(const char *text, size_t len) {
    static const char exact[] = "cmp%1,%2;sbb%0,%0";
    char compact[96];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    /* Original GNU Linux 6.6.143 barrier.h ends SBB with a semicolon.
     * Permit exactly one trailing delimiter, not extra machine opcodes. */
    return strcmp(compact, exact) == 0 ||
           strcmp(compact, "cmp%1,%2;sbb%0,%0;") == 0;
}

static bool target_constraint_is(const MinicInlineAsmOperand *op, const char *s) {
    const size_t n = s ? strlen(s) : 0U;
    return op && s && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, s, n) == 0;
}

/* Linux _THIS_IP_: one precise RIP-relative LEA with a 64-bit register
 * result, never an arbitrary target inline ASM escape hatch. This reaches
 * Core as an address-backed REGISTER_OUTPUT structured effect, not a new
 * frontend IR opcode or a Core-architecture conditional. */
bool minic_x86_64_linux_ip_here_template(const char *text, size_t length) {
    static const char exact[] = "lea0(%%rip),%0";
    char compact[48];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0')
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}

static bool x86_linux_ip_here_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *dst;
    MinicType plain;
    if (program == NULL || src == NULL || contract == NULL ||
        src->is_goto || src->output_count != 1U ||
        src->input_count != 0U || src->outputs == NULL ||
        src->clobber_count != 0U || src->register_clobber_count != 0U ||
        src->has_memory_clobber || src->label_count != 0U ||
        src->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        !target_constraint_is(&src->outputs[0], "=r") ||
        !minic_x86_64_linux_ip_here_template(
            src->template_text, src->template_length))
        return false;
    dst = minic_c0_program_expression(program, src->outputs[0].expression);
    if (dst == NULL || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !minic_type_unqualified(dst->type, &plain) ||
        (!minic_type_equal(plain, minic_type_unsigned_long()) &&
         !minic_type_equal(plain, minic_type_long()) &&
         !minic_type_equal(plain, minic_type_unsigned_long_long()) &&
         !minic_type_equal(plain, minic_type_long_long())))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

/* Original x86 Linux this_cpu_read_stable_8 is a nonvolatile GNU p
 * address computation followed by a REAL GS-segment hardware read.
 * Only the literal 64-bit instruction / two exact operands are admitted.
 * A Core address SSA value represents P[var]; x86 still owns the GS ISA. */
size_t minic_x86_64_asm_percpu_stable_width(const char *s, size_t length) {
    static const struct { const char *name; size_t width; } forms[] = {
        {"movb%%gs:%P[var],%[val]",1U}, {"movb%%gs:%P1,%0",1U},
        {"movw%%gs:%P[var],%[val]",2U}, {"movw%%gs:%P1,%0",2U},
        {"movl%%gs:%P[var],%[val]",4U}, {"movl%%gs:%P1,%0",4U},
        {"movq%%gs:%P[var],%[val]",8U}, {"movq%%gs:%P1,%0",8U}
    };
    char compact[96]; size_t i, n = 0U;
    if (!s || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = s[i];
        if (c == ';' || c == '\r' || c == '\n') return 0U;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(forms[i].name, compact) == 0) return forms[i].width;
    return 0U;
}
bool minic_x86_64_asm_percpu_stable_template(const char *s, size_t length) {
    return minic_x86_64_asm_percpu_stable_width(s, length) != 0U;
}
static bool x86_percpu_source(const MinicC0Program *program,
                              const MinicInlineAsm *src) {
    const MinicExpression *out, *in;
    MinicType type;
    const size_t width = src ? minic_x86_64_asm_percpu_stable_width(
        src->template_text, src->template_length) : 0U;
    if (!program || !src || src->is_goto || src->is_volatile ||
        src->output_count != 1U || src->input_count != 1U ||
        !src->outputs || !src->inputs ||
        src->clobber_count != 0U || src->register_clobber_count != 0U ||
        src->has_memory_clobber || src->label_count != 0U ||
        width == 0U ||
        !target_constraint_is(&src->outputs[0], width == 1U ? "=q" : "=r") ||
        !target_constraint_is(&src->inputs[0], "p"))
        return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    in = minic_c0_program_expression(program, src->inputs[0].expression);
    return out && in && out->value_category == MINIC_VALUE_LVALUE &&
           !minic_type_is_const(out->type) &&
           minic_type_unqualified(out->type, &type) &&
           (minic_type_is_pointer(type) ? width == 8U :
            minic_type_is_integer(type) && type.base_kind == MINIC_TYPE_BASE_INT &&
            (width == 1U ? type.integer_rank == MINIC_INTEGER_RANK_CHAR :
             width == 2U ? type.integer_rank == MINIC_INTEGER_RANK_SHORT :
             width == 4U ? type.integer_rank == MINIC_INTEGER_RANK_INT :
             (type.integer_rank == MINIC_INTEGER_RANK_LONG ||
              type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG))) &&
           minic_type_is_pointer(in->type);
}
static bool c_is(const MinicInlineAsmOperand *op, const char *s) {
    return target_constraint_is(op, s);
}
static bool clobber_is(const MinicInlineAsmRegisterClobber *op, const char *s) {
    const size_t n = strlen(s);
    return op->name && op->name_length == n && memcmp(op->name, s, n) == 0;
}
/* Whitespace is irrelevant for these four specific instructions, whose
 * operand constraints and exact instruction sequence are checked together. */
/* Only integer operands of the same real x86 width; never silently
 * convert a user-supplied operand or drop the hardware condition flags. */

/* x86 Linux zero-GNU-operand hardware ordering / processor-state class.
 * One ISA descriptor handles source admission and final machine emission:
 * 1=locked stack read-write barrier; 2/3/4=full/load/store fence;
 * 5=pause, 6=rep;nop, 7=x87 FNINIT. No unrestricted ASM text is allowed. */
unsigned int minic_x86_64_linux_zero_barrier_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "lock;addl$0,-4(%%rsp)",
        "mfence", "lfence", "sfence", "pause", "rep;nop", "fninit"
    };
    char compact[64];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == '\0') return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(exact) / sizeof(exact[0]); ++i)
        if (strcmp(exact[i], compact) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
}

static bool x86_linux_zero_barrier_source(
    const MinicInlineAsm *source, MinicTargetInlineAsmContract *contract) {
    unsigned int form;
    bool mem;
    if (source == NULL || contract == NULL || !source->is_volatile ||
        source->is_goto || source->input_count != 0U ||
        source->output_count != 0U || source->label_count != 0U)
        return false;
    form = minic_x86_64_linux_zero_barrier_form(
        source->template_text, source->template_length);
    if (form == 0U) return false;
    mem = source->has_memory_clobber;
    if (form <= 4U && !mem) return false;
    if (form == 1U) {
        if (source->clobber_count != 2U ||
            source->register_clobber_count != 1U ||
            source->register_clobbers == NULL ||
            !clobber_is(&source->register_clobbers[0], "cc"))
            return false;
    } else if (source->register_clobber_count != 0U ||
               source->clobber_count != (mem ? 1U : 0U)) {
        return false;
    }
    contract->operand_count = 0U;
    return true;
}

/* Exact original GNU Linux 6.6.143 rdtsc_ordered() hardware clock:
 * the baseline instruction and two alternative patch records are integral
 * to the Linux ELF object. Qualify only the entire original 652-byte
 * source template, never general-purpose GNU ASM passthrough. */
static bool x86_ordered_clock_form(const char *s, size_t len) {
    static const char expected[] =
        "# ALT: oldinstr2\n661:\n\trdtsc\n662:\n# ALT: padding2\n.skip -((((6651"
        "f-6641f) ^ (((6651f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) <"
        " (6652f-6642f))))) - (662b-661b)) > 0) * (((6651f-6641f) ^ (((665"
        "1f-6641f) ^ (6652f-6642f)) & -(-((6651f-6641f) < (6652f-6642f))))"
        ") - (662b-661b)), 0x90\n663:\n.pushsection .altinstructions,\"a\"\n .l"
        "ong 661b - .\n .long 6641f - .\n .4byte (20*32+ 2)\n .byte 663b-661b"
        "\n .byte 6651f-6641f\n .long 661b - .\n .long 6642f - .\n .4byte ( 1*"
        "32+27)\n .byte 663b-661b\n .byte 6652f-6642f\n.popsection\n.pushsecti"
        "on .altinstr_replacement, \"ax\"\n# ALT: replacement 1\n6641:\n\tlfence"
        "; rdtsc\n6651:\n# ALT: replacement 2\n6642:\n\trdtscp\n6652:\n.popsectio"
        "n\n";
    return s != NULL && len == sizeof(expected) - 1U &&
           memcmp(s, expected, sizeof(expected) - 1U) == 0;
}
/* Complete x86 EDX:EAX counter family, GNU 32/64-bit output carriers. */
unsigned int minic_x86_64_linux_counter_form(const char *s,size_t len) {
    static const char *const names[]={"rdtsc","rdpmc","rdtscp","xgetbv"};
    char buf[32];size_t i,n=0U;
    if (x86_ordered_clock_form(s,len)) return 4U;
    if(!s||!len||len>=sizeof(buf))return 0U;
    for(i=0U;i<len;++i) {
        const char c=s[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(n+1U>=sizeof(buf))return 0U;
        buf[n++]=c;
    }
    buf[n]=0;
    for(i=0U;i<4U;++i)
        if(strcmp(buf,names[i])==0)
            return i==3U?5U:(unsigned int)i+1U;
    return 0U;
}
static bool x86_counter_source(const MinicC0Program *p,const MinicInlineAsm *s,
                               MinicTargetInlineAsmContract *ct) {
    const unsigned int f=s?minic_x86_64_linux_counter_form(
        s->template_text,s->template_length):0U;
    const MinicExpression *e0,*e1,*v;
    MinicType t0,t1,ti;
    if(!p||!s||!ct||!f||!s->is_volatile||s->is_goto||
       !s->outputs||s->output_count!=2U||
       s->input_count!=((f==2U||f==5U)?1U:0U)||
       ((f==2U||f==5U)&&!s->inputs)||
       s->label_count||s->has_memory_clobber||
       s->clobber_count!=((f==3U||f==4U)?1U:0U)||
       s->register_clobber_count!=((f==3U||f==4U)?1U:0U)||
       ((f==3U||f==4U)&&(!s->register_clobbers||
        !clobber_is(&s->register_clobbers[0],"ecx")))||
       !c_is(&s->outputs[0],"=a")||!c_is(&s->outputs[1],"=d")||
       ((f==2U||f==5U)&&!c_is(&s->inputs[0],"c")))return false;
    e0=minic_c0_program_expression(p,s->outputs[0].expression);
    e1=minic_c0_program_expression(p,s->outputs[1].expression);
    if(!e0||!e1||e0->value_category!=MINIC_VALUE_LVALUE||
       e1->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(e0->type)||minic_type_is_const(e1->type)||
       !minic_type_unqualified(e0->type,&t0)||
       !minic_type_unqualified(e1->type,&t1)||
       !minic_type_equal(t0,t1)||t0.base_kind!=MINIC_TYPE_BASE_INT||
       !minic_type_is_integer(t0)||t0.pointer_depth||
       (t0.integer_rank!=MINIC_INTEGER_RANK_INT&&
        t0.integer_rank!=MINIC_INTEGER_RANK_LONG&&
        t0.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG))return false;
    if(f==2U||f==5U){
        v=minic_c0_program_expression(p,s->inputs[0].expression);
        if(!v||!minic_type_unqualified(v->type,&ti)||
           ti.base_kind!=MINIC_TYPE_BASE_INT||
           !minic_type_is_integer(ti)||ti.pointer_depth||
           ti.integer_rank!=MINIC_INTEGER_RANK_INT)return false;
    }
    ct->operand_count=(f==2U||f==5U)?3U:2U;
    ct->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ct->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    if(f==2U||f==5U)
        ct->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* XCR0/XSS write is architecturally privileged and requires all three
 * physical 32-bit input registers. A plain C call or memory store would not
 * update extended processor state, so keep its effect in the x86 target. */
static bool x86_xsetbv_source(const MinicC0Program *program,
                             const MinicInlineAsm *source,
                             MinicTargetInlineAsmContract *contract) {
    static const char *const constraints[] = {"a", "d", "c"};
    size_t i;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->template_length != 6U ||
        memcmp(source->template_text, "xsetbv", 6U) != 0 ||
        source->output_count != 0U || source->input_count != 3U ||
        !source->inputs || source->label_count != 0U ||
        source->clobber_count != 0U || source->has_memory_clobber ||
        source->register_clobber_count != 0U) return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicExpression *expression = minic_c0_program_expression(
            program, source->inputs[i].expression);
        MinicType type;
        if (!target_constraint_is(&source->inputs[i], constraints[i]) ||
            source->inputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            !expression || !minic_type_unqualified(expression->type, &type) ||
            !minic_type_is_integer(type) || type.pointer_depth != 0U ||
            type.integer_rank != MINIC_INTEGER_RANK_INT) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 3U;
    return true;
}

/* FXSAVE uses a 16-byte-aligned, 512-byte architectural memory image.
 * Linux has both the original 32-bit pointer form (`fxsave %0`) and the
 * 64-bit pointer form (`fxsaveq %[fx]`). A structured memory output records
 * exactly which C object is overwritten; no scalar imitation is permitted. */
unsigned int minic_x86_64_linux_fxsave_form(
    const char *text, size_t length) {
    if (!text) return 0U;
    if (length == 9U && memcmp(text, "fxsave %0", 9U) == 0)
        return 1U;
    if (length == 13U && memcmp(text, "fxsaveq %[fx]", 13U) == 0)
        return 2U;
    /* Core canonicalizes a named GNU output to its numeric binding. Only
     * the source qualifier can admit the named form in the first place. */
    if (length == 10U && memcmp(text, "fxsaveq %0", 10U) == 0)
        return 2U;
    return 0U;
}

static bool x86_fxsave_source(const MinicC0Program *program,
                             const MinicInlineAsm *source,
                             MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_fxsave_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *destination;
    size_t size, alignment;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        !source->outputs || source->output_count != 1U ||
        source->input_count != 0U || source->label_count != 0U ||
        source->has_memory_clobber || source->clobber_count != 0U ||
        source->register_clobber_count != 0U ||
        !target_constraint_is(&source->outputs[0], form == 1U ? "+m" : "=m") ||
        source->outputs[0].access !=
            (form == 1U ? MINIC_INLINE_ASM_OPERAND_READ_WRITE :
                          MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)) return false;
    if (form == 2U &&
        (!source->outputs[0].name || source->outputs[0].name_length != 2U ||
         memcmp(source->outputs[0].name, "fx", 2U) != 0)) return false;
    destination = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!destination || destination->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(destination->type) ||
        !minic_data_layout_type(
            minic_target_info_data_layout(minic_target_info_for_architecture(
                MINIC_TARGET_ARCH_X86_64)),
            program, destination->type, &size, &alignment) ||
        size < 512U || alignment < 16U) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form == 1U ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    return true;
}

/* Early boot probes the x87 execution unit by FNINIT and captures its
 * 16-bit status/control words in two independent writable C memory objects.
 * GNU `+m` makes both locations read/write effects, not register outputs. */
bool minic_x86_64_linux_x87_probe_form(
    const char *text, size_t length) {
    static const char expected[] = "fninit ; fnstsw %0 ; fnstcw %1";
    return text && length == sizeof(expected) - 1U &&
           memcmp(text, expected, sizeof(expected) - 1U) == 0;
}

static bool x86_x87_probe_source(const MinicC0Program *program,
                                 const MinicInlineAsm *source,
                                 MinicTargetInlineAsmContract *contract) {
    size_t i;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || !minic_x86_64_linux_x87_probe_form(
            source->template_text, source->template_length) ||
        !source->outputs || source->output_count != 2U ||
        source->input_count != 0U || source->label_count != 0U ||
        source->has_memory_clobber || source->clobber_count != 0U ||
        source->register_clobber_count != 0U) return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicExpression *destination = minic_c0_program_expression(
            program, source->outputs[i].expression);
        MinicType value_type;
        if (!target_constraint_is(&source->outputs[i], "+m") ||
            source->outputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_WRITE ||
            !destination || destination->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(destination->type) ||
            !minic_type_unqualified(destination->type, &value_type) ||
            !minic_type_equal(value_type, minic_type_unsigned_short()))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    }
    contract->operand_count = 2U;
    contract->implicit_memory_effect = true;
    return true;
}


/* Original Linux 6.6 ALTERNATIVE(call __sw_hweight{32,64}, POPCNT).
 * This source parser checks the COMPLETE GNU alternative assembly,
 * including patch metadata/feature=151, not a prefix/suffix substring.
 * Real kernel software fallback and ALT ELF records must be preserved. */
unsigned int minic_x86_64_hweight_alternative_form(const char *s,size_t len) {
    static const char *const begin="661:call__sw_hweight";
    static const char *const middle=
        "662:.skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-."
        ".4byte(4*32+23)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "6641:popcnt";
    char compact[1024],expect[1024];size_t i,n=0U,w;
    if(!s||!len||len>=sizeof(compact))return 0U;
    for(i=0U;i<len;++i){
        if(s[i]=='#'){while(i<len&&s[i]!=10)++i;continue;}
        if(s[i]==' '||s[i]==9||s[i]==10||s[i]==13)continue;
        if(n+1U>=sizeof(compact))return 0U;
        compact[n++]=s[i];
    }
    compact[n]=0;
    for(w=4U;w<=8U;w+=4U) {
        const int written=snprintf(expect,sizeof(expect),
           "%s%s%s%s%%1,%%0""6651:.popsection",
           begin,w==4U?"32":"64",middle,w==4U?"l":"q");
        if(written>0&&(size_t)written<sizeof(expect)&&
           strcmp(compact,expect)==0)return (unsigned int)w;
    }
    return 0U;
}
static bool x86_hweight_source(const MinicC0Program *program,
    const MinicInlineAsm *s,MinicTargetInlineAsmContract *ct){
    const unsigned int w=s?minic_x86_64_hweight_alternative_form(
       s->template_text,s->template_length):0U;
    const MinicExpression *out,*arg;MinicType to,ti;
    if(!program||!s||!ct||!w||s->is_volatile||s->is_goto||
       s->has_memory_clobber||s->label_count||s->clobber_count||
       s->register_clobber_count||s->output_count!=1U||
       s->input_count!=1U||!s->outputs||!s->inputs||
       !c_is(&s->outputs[0],"=a")||!c_is(&s->inputs[0],"D"))return false;
    out=minic_c0_program_expression(program,s->outputs[0].expression);
    arg=minic_c0_program_expression(program,s->inputs[0].expression);
    if(!out||!arg||out->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(out->type)||
       !minic_type_unqualified(out->type,&to)||
       !minic_type_unqualified(arg->type,&ti)||
       to.base_kind!=MINIC_TYPE_BASE_INT||
       ti.base_kind!=MINIC_TYPE_BASE_INT||
       !minic_type_is_integer(to)||!minic_type_is_integer(ti)||
       to.pointer_depth||ti.pointer_depth)return false;
    if(w==4U?
       (to.integer_rank!=MINIC_INTEGER_RANK_INT||
        ti.integer_rank!=MINIC_INTEGER_RANK_INT):
       ((to.integer_rank!=MINIC_INTEGER_RANK_LONG&&
         to.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)||
        (ti.integer_rank!=MINIC_INTEGER_RANK_LONG&&
         ti.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)))return false;
    ct->operand_count=2U;
    ct->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ct->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


/* Original Linux asm/msr.h Linux 6.6 __rdmsr/__wrmsr:
 * Only the EXACT 1: RDMSR/WRMSR; 2: original GNU exception table
 * (two PC-relative exception entries, fixup type 9/8) may be executed.
 * This is privileged machine ISA, never emulated by a plain C load. */
unsigned int minic_x86_64_linux_msr_form(const char *text,size_t len) {
    static const char *const exact[]={
      "1:rdmsr2:.pushsection\"__ex_table\",\"a\".balign4"
      ".long(1b)-..long(2b)-..long9.popsection",
      "1:wrmsr2:.pushsection\"__ex_table\",\"a\".balign4"
      ".long(1b)-..long(2b)-..long8.popsection"
    };
    /* Source named operands are canonicalized to numeric %0 by Core.
     * Exact matching of both representations preserves the hardware and
     * full GNU exception-register fixup contract. */
    static const char safe_read[] = "1: rdmsr ; xor %[err],%[err]\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%[err], type=11 \n.purgem extable_type_reg\n .popsection\n";
    static const char safe_write[] = "1: wrmsr ; xor %[err],%[err]\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%[err], type=10 \n.purgem extable_type_reg\n .popsection\n";
    static const char canonical_read[] = "1: rdmsr ; xor %0,%0\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%0, type=11 \n.purgem extable_type_reg\n .popsection\n";
    static const char canonical_write[] = "1: wrmsr ; xor %0,%0\n2:\n\t .pushsection \"__ex_table\",\"a\"\n .balign 4\n .long (1b) - .\n .long (2b) - .\n.macro extable_type_reg type:req reg:req\n.set .Lfound, 0\n.set .Lregnr, 0\n.irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.set .Lregnr, 0\n.irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n.ifc \\reg, %%\\rs\n.set .Lfound, .Lfound+1\n.long \\type + (.Lregnr << 8)\n.endif\n.set .Lregnr, .Lregnr+1\n.endr\n.if (.Lfound != 1)\n.error \"extable_type_reg: bad register argument\"\n.endif\n.endm\nextable_type_reg reg=%0, type=10 \n.purgem extable_type_reg\n .popsection\n";
    if(text && len==sizeof(safe_read)-1U &&
       memcmp(text,safe_read,sizeof(safe_read)-1U)==0)return 3U;
    if(text && len==sizeof(safe_write)-1U &&
       memcmp(text,safe_write,sizeof(safe_write)-1U)==0)return 4U;
    if(text && len==sizeof(canonical_read)-1U &&
       memcmp(text,canonical_read,sizeof(canonical_read)-1U)==0)return 3U;
    if(text && len==sizeof(canonical_write)-1U &&
       memcmp(text,canonical_write,sizeof(canonical_write)-1U)==0)return 4U;
    char norm[256];size_t i,n=0U;
    if(!text||!len||len>=sizeof(norm))return 0U;
    for(i=0U;i<len;++i){
       const char c=text[i];
       if(c==' '||c==9||c==10||c==13)continue;
       if(n+1U>=sizeof(norm))return 0U;
       norm[n++]=c;
    }
    norm[n]=0;
    for(i=0U;i<2U;++i)
        if(strcmp(norm,exact[i])==0)return (unsigned int)i+1U;
    return 0U;
}
static bool x86_linux_msr_source(const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract) {
    const unsigned int form=src?minic_x86_64_linux_msr_form(
        src->template_text,src->template_length):0U;
    const MinicExpression *e;MinicType type;
    size_t i;
    if(!program||!src||!contract||!form)return false;
    if(form==3U || form==4U) {
        const size_t outputs=form==3U?3U:1U;
        const size_t inputs=form==3U?1U:3U;
        if(!src->is_volatile || src->is_goto || src->label_count ||
           !src->outputs || !src->inputs ||
           src->output_count!=outputs || src->input_count!=inputs ||
           src->clobber_count!=(form==4U?1U:0U) ||
           src->has_memory_clobber!=(form==4U) ||
           src->register_clobber_count ||
           !c_is(&src->outputs[0],form==3U?"=r":"=a") ||
           (form==3U && (!c_is(&src->outputs[1],"=a") ||
                          !c_is(&src->outputs[2],"=d"))) ||
           !c_is(&src->inputs[0],"c") ||
           (form==4U && (!c_is(&src->inputs[1],"0") ||
                          !c_is(&src->inputs[2],"d"))))return false;
        for(i=0U;i<outputs+inputs;++i) {
            const bool output=i<outputs;
            const MinicInlineAsmOperand *op=output ? &src->outputs[i] :
                &src->inputs[i-outputs];
            const MinicExpression *v=minic_c0_program_expression(program,op->expression);
            MinicType vt;
            const bool wide=form==3U && output && i!=0U;
            if(!v || !minic_type_unqualified(v->type,&vt) ||
               !minic_type_is_integer(vt) || vt.pointer_depth ||
               vt.base_kind!=MINIC_TYPE_BASE_INT ||
               vt.integer_rank!=(wide?MINIC_INTEGER_RANK_LONG:MINIC_INTEGER_RANK_INT) ||
               (output && (v->value_category!=MINIC_VALUE_LVALUE ||
                           minic_type_is_const(v->type))))return false;
        }
        contract->operand_count=outputs+inputs;
        for(i=0U;i<contract->operand_count;++i)
            contract->kinds[i]=i<outputs?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if(!program||!src||!contract||!form||!src->is_volatile||
       src->is_goto||src->label_count||
       src->output_count!=(form==1U?2U:0U)||
       src->input_count!=(form==1U?1U:3U)||
       (form==1U&&!src->outputs)||!src->inputs||
       src->clobber_count!=(form==1U?0U:1U)||
       src->has_memory_clobber!=(form==2U)||
       src->register_clobber_count)return false;
    if(form==1U) {
        if(!c_is(&src->outputs[0],"=a")||
           !c_is(&src->outputs[1],"=d")||
           !c_is(&src->inputs[0],"c"))return false;
    }else if(!c_is(&src->inputs[0],"c")||
             !c_is(&src->inputs[1],"a")||
             !c_is(&src->inputs[2],"d"))return false;
    for(i=0U;i<src->output_count+src->input_count;++i){
        const bool output=i<src->output_count;
        const MinicInlineAsmOperand *op=output?
            &src->outputs[i]:&src->inputs[i-src->output_count];
        e=minic_c0_program_expression(program,op->expression);
        if(!e||!minic_type_unqualified(e->type,&type)||
           type.base_kind!=MINIC_TYPE_BASE_INT||
           !minic_type_is_integer(type)||type.pointer_depth||
           (output&&(e->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(e->type)))||
           (form==1U&&output?
              (type.integer_rank!=MINIC_INTEGER_RANK_LONG&&
               type.integer_rank!=MINIC_INTEGER_RANK_INT):
              type.integer_rank!=MINIC_INTEGER_RANK_INT))
            return false;
    }
    contract->operand_count=src->output_count+src->input_count;
    for(i=0U;i<contract->operand_count;++i)
        contract->kinds[i]=i<src->output_count?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


/* Linux checksum_64.h ip_fast_csum: full original add/adc/loop/CF chain.
 * This machine form changes output pointer and header-length registers too;
 * it is not the simpler two-input csum_fold contract. */
bool minic_x86_64_ip_fast_csum_template(const char *text, size_t length) {
    static const char expected[] =
        "movl(%1),%0subl$4,%2jbe2faddl4(%1),%0adcl8(%1),%0"
        "adcl12(%1),%01:adcl16(%1),%0lea4(%1),%1decl%2jne1b"
        "adcl$0,%0movl%0,%2shrl$16,%0addw%w2,%w0adcl$0,%0notl%02:";
    char compact[256]; size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i=0U;i<length;++i) {
        const char c=text[i];
        if (c==' ' || c=='\n' || c=='\t' || c=='\r') continue;
        if (n+1U>=sizeof(compact)) return false;
        compact[n++]=c;
    }
    compact[n]='\0';
    return strcmp(compact, expected)==0;
}
static bool x86_ip_fast_csum_source(const MinicC0Program *p,
    const MinicInlineAsm *s, MinicTargetInlineAsmContract *ct) {
    const MinicExpression *expr;
    MinicType type;
    size_t i;
    if (!p || !s || !ct || s->is_volatile || s->is_goto ||
        !minic_x86_64_ip_fast_csum_template(s->template_text,s->template_length) ||
        !s->outputs || !s->inputs || s->output_count!=3U ||
        s->input_count!=2U || !s->has_memory_clobber ||
        s->clobber_count!=1U || s->register_clobber_count || s->label_count)
        return false;
    for(i=0U;i<3U;++i) {
        if(!c_is(&s->outputs[i],"=r"))return false;
        expr=minic_c0_program_expression(p,s->outputs[i].expression);
        if(!expr || expr->value_category!=MINIC_VALUE_LVALUE ||
            minic_type_is_const(expr->type) ||
            !minic_type_unqualified(expr->type,&type) ||
            (i==1U ? !minic_type_is_pointer(type) :
                (!minic_type_is_integer(type) ||
                 type.integer_rank!=MINIC_INTEGER_RANK_INT))) return false;
    }
    if (!c_is(&s->inputs[0],"1") || !c_is(&s->inputs[1],"2"))return false;
    for(i=0U;i<2U;++i) {
        expr=minic_c0_program_expression(p,s->inputs[i].expression);
        if(!expr || !minic_type_unqualified(expr->type,&type) ||
           (i==0U ? !minic_type_is_pointer(type) :
             (!minic_type_is_integer(type) ||
              type.integer_rank!=MINIC_INTEGER_RANK_INT)))return false;
    }
    ct->operand_count=5U;
    for(i=0U;i<5U;++i)
        ct->kinds[i]=i<3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* Original Linux x86 checksum_64.h csum_fold: 32-bit ADD then ADC
 * with a tied initialization operand; the real CF hardware effect is
 * indivisible. This strict ISA contract handles the function family. */
bool minic_x86_64_checksum_fold_template(const char *text,size_t len){
    static const char *const expected="addl%1,%0adcl$0xffff,%0";
    char compact[80];size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return false;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(c==';'||n+1U>=sizeof(compact))return false;
        compact[n++]=c;
    }
    compact[n]=0;
    return strcmp(compact,expected)==0;
}
static bool x86_checksum_source(const MinicC0Program *p,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *ct){
    size_t i;
    if(!p||!src||!ct||src->is_volatile||src->is_goto||
       src->output_count!=1U||src->input_count!=2U||
       !src->outputs||!src->inputs||src->label_count||
       src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       !minic_x86_64_checksum_fold_template(
           src->template_text,src->template_length)||
       !c_is(&src->outputs[0],"=r")||
       !c_is(&src->inputs[0],"r")||!c_is(&src->inputs[1],"0"))
        return false;
    for(i=0U;i<3U;++i){
        const MinicInlineAsmOperand *op=i==0U?
            &src->outputs[0]:&src->inputs[i-1U];
        const MinicExpression *e=minic_c0_program_expression(
            p,op->expression);
        MinicType ty;
        if(!e||!minic_type_unqualified(e->type,&ty)||
           ty.base_kind!=MINIC_TYPE_BASE_INT||
           ty.integer_rank!=MINIC_INTEGER_RANK_INT||
           (i==0U&&(e->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(e->type))))
            return false;
    }
    ct->operand_count=3U;
    ct->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    ct->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    ct->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


/* One typed Linux x86 per-CPU GS instruction catalog.
 * Matches the original arch/x86/include/asm/percpu.h GNU operand modes.
 * Only the target owns the GS segment and width/ALU instruction names. */
typedef struct {
    const char *name;
    unsigned int op;  /* read=0 write=1 inc=2 dec=3 add=4 sub=5
                         and=6 or=7 xor=8 */
    size_t width;
} MinicX86PerCpuForm;
static const MinicX86PerCpuForm x86_percpu_forms[]={
    {"mov",0U,1U},{"mov",0U,2U},{"mov",0U,4U},{"mov",0U,8U},
    {"mov",1U,1U},{"mov",1U,2U},{"mov",1U,4U},{"mov",1U,8U},
    {"inc",2U,1U},{"inc",2U,2U},{"inc",2U,4U},{"inc",2U,8U},
    {"dec",3U,1U},{"dec",3U,2U},{"dec",3U,4U},{"dec",3U,8U},
    {"add",4U,1U},{"add",4U,2U},{"add",4U,4U},{"add",4U,8U},
    {"sub",5U,1U},{"sub",5U,2U},{"sub",5U,4U},{"sub",5U,8U},
    {"and",6U,1U},{"and",6U,2U},{"and",6U,4U},{"and",6U,8U},
    {"or",7U,1U},{"or",7U,2U},{"or",7U,4U},{"or",7U,8U},
    {"xor",8U,1U},{"xor",8U,2U},{"xor",8U,4U},{"xor",8U,8U}
};
bool minic_x86_64_linux_percpu_form(const char *text,size_t length,
    unsigned int *op,size_t *width){
    char compact[128],exact[128];size_t i,j=0U;
    if(!text||!length||length>=sizeof(compact)||!op||!width)
        return false;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(c==';'||j+1U>=sizeof(compact))return false;
        compact[j++]=c;
    }
    compact[j]=0;
    for(i=0U;i<sizeof(x86_percpu_forms)/sizeof(x86_percpu_forms[0]);++i){
        const MinicX86PerCpuForm *f=&x86_percpu_forms[i];
        const char suffix=f->width==1U?'b':f->width==2U?'w':
                          f->width==4U?'l':'q';
        int n;
        if(f->op==0U)
            n=snprintf(exact,sizeof(exact),"%s%c%%%%gs:%%1,%%0",
                       f->name,suffix);
        else if(f->op==2U||f->op==3U)
            n=snprintf(exact,sizeof(exact),"%s%c%%%%gs:%%0",
                       f->name,suffix);
        else
            n=snprintf(exact,sizeof(exact),"%s%c%%1,%%%%gs:%%0",
                       f->name,suffix);
        if(n>0&&(size_t)n<sizeof(exact)&&strcmp(compact,exact)==0){
            *op=f->op;*width=f->width;return true;
        }
        /* Linux original C source uses named %[var]/%[val].
         * Numeric templates also arise in exact preprocessed .i.
         * Check the complete source shape, not an instruction prefix. */
        if(f->op==0U)
            n=snprintf(exact,sizeof(exact),
                       "%s%c%%%%gs:%%[var],%%[val]",f->name,suffix);
        else if(f->op==2U||f->op==3U)
            n=snprintf(exact,sizeof(exact),
                       "%s%c%%%%gs:%%[var]",f->name,suffix);
        else
            n=snprintf(exact,sizeof(exact),
                       "%s%c%%[val],%%%%gs:%%[var]",f->name,suffix);
        if(n>0&&(size_t)n<sizeof(exact)&&strcmp(compact,exact)==0){
            *op=f->op;*width=f->width;return true;
        }
    }
    return false;
}
static bool x86_percpu_gs_source(const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract){
    unsigned int op;size_t width,i;
    const MinicExpression *e;
    if(!program||!src||!contract||src->is_goto||src->label_count||
       src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       !minic_x86_64_linux_percpu_form(
         src->template_text,src->template_length,&op,&width)||
       src->output_count!=1U||
       src->input_count!=((op==2U||op==3U)?0U:1U)||
       !src->outputs||(src->input_count&&!src->inputs))
        return false;
    if(op==0U){
        if(!c_is(&src->outputs[0],width==1U?"=q":"=r")||
           !c_is(&src->inputs[0],"m"))return false;
    }else if(!c_is(&src->outputs[0],"+m")||
             (src->input_count&&!c_is(&src->inputs[0],
                width==1U?"qi":width==8U?"re":"ri")))
        return false;
    for(i=0U;i<1U+src->input_count;++i){
        const MinicInlineAsmOperand *a=i==0U?
            &src->outputs[0]:&src->inputs[0];
        MinicType ty;
        e=minic_c0_program_expression(program,a->expression);
        if(!e||!minic_type_unqualified(e->type,&ty)||
           /* A GNU per-CPU pointer slot is eight bytes, despite its
            * declared lvalue type not being integer. Other widths
            * and operations retain strict original type qualification. */
           (!(width==1U && minic_type_equal(ty,minic_type_bool())) &&
            !(width==4U && minic_type_is_enum(ty)) &&
            !(width==8U && minic_type_is_pointer(ty) &&
              ((op==0U && i==1U) || (op==1U && i==0U))) &&
            (ty.base_kind!=MINIC_TYPE_BASE_INT||
             (width==1U?ty.integer_rank!=MINIC_INTEGER_RANK_CHAR:
              width==2U?ty.integer_rank!=MINIC_INTEGER_RANK_SHORT:
              width==4U?ty.integer_rank!=MINIC_INTEGER_RANK_INT:
                 (ty.integer_rank!=MINIC_INTEGER_RANK_LONG &&
                  ty.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG))))||
           (i==0U&&(e->value_category!=MINIC_VALUE_LVALUE||
                     (op==0U&&minic_type_is_const(e->type)))))
            return false;
    }
    contract->operand_count=1U+src->input_count;
    contract->kinds[0]=op==0U?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    if(src->input_count)
        contract->kinds[1]=op==0U?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


/* Kernel this_cpu_ptr: a live register seed combined with the GS-relative
 * per-CPU displacement, using GNU tied output 0 / memory input 1. Unlike
 * the 36 direct GS lvalue operations, the output is an ordinary C scalar.
 * One exact semantic descriptor covers 5 ALU ops x 32/64-bit slots and
 * optionally their explicit size suffix; no generic GS passthrough. */
unsigned int minic_x86_64_gs_tied_alu_form(
    const char *text, size_t length, size_t *width) {
    static const char *const forms[]={"add","sub","and","or","xor"};
    char compact[80], expected[80];
    size_t i, n = 0U, op, w;
    if (!text || !length || length >= sizeof(compact) || !width)
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0' ||
            n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (op = 0U; op < 5U; ++op)
        for (w = 4U; w <= 8U; w += 4U) {
            const char suffix = w == 4U ? 'l' : 'q';
            int size = snprintf(expected,sizeof(expected),
                                "%s%%%%gs:%%1,%%0",forms[op]);
            if (size > 0 && (size_t)size < sizeof(expected) &&
                strcmp(compact,expected)==0) {
                /* Unsuffixed GNU ALU operand width is selected by the
                 * output register class, validated independently below. */
                *width=0U;
                return (unsigned int)(op+1U);
            }
            size=snprintf(expected,sizeof(expected),
                          "%s%c%%%%gs:%%1,%%0",forms[op],suffix);
            if(size>0&&(size_t)size<sizeof(expected)&&
               strcmp(compact,expected)==0) {
                *width=w;
                return (unsigned int)(op+1U);
            }
        }
    return 0U;
}

static bool x86_gs_tied_alu_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *output, *mem, *seed;
    MinicType out_type, mem_type, seed_type;
    size_t opcode_width=0U, width;
    unsigned int op;
    if (program == NULL || src == NULL || contract == NULL ||
        src->is_goto || src->is_volatile ||
        src->output_count != 1U || src->input_count != 2U ||
        src->outputs == NULL || src->inputs == NULL ||
        src->label_count || src->clobber_count ||
        src->register_clobber_count || src->has_memory_clobber ||
        !c_is(&src->outputs[0],"=r") ||
        !c_is(&src->inputs[0],"m") ||
        !c_is(&src->inputs[1],"0"))
        return false;
    op=minic_x86_64_gs_tied_alu_form(
        src->template_text, src->template_length, &opcode_width);
    if (!op) return false;
    output=minic_c0_program_expression(program,src->outputs[0].expression);
    mem=minic_c0_program_expression(program,src->inputs[0].expression);
    seed=minic_c0_program_expression(program,src->inputs[1].expression);
    if (!output || !mem || !seed ||
        output->value_category != MINIC_VALUE_LVALUE ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_unqualified(output->type,&out_type) ||
        !minic_type_unqualified(mem->type,&mem_type) ||
        !minic_type_unqualified(seed->type,&seed_type) ||
        !minic_type_is_integer(out_type) ||
        (out_type.integer_rank != MINIC_INTEGER_RANK_INT &&
         out_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
         out_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    width = out_type.integer_rank==MINIC_INTEGER_RANK_INT ? 4U : 8U;
    if (opcode_width != 0U && opcode_width != width)
        return false;
    if (!minic_type_is_integer(mem_type) ||
        (width==4U
          ? mem_type.integer_rank != MINIC_INTEGER_RANK_INT
          : mem_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
            mem_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    if (minic_type_is_pointer(seed_type)) {
        if (width != 8U) return false;
    } else if (!minic_type_is_integer(seed_type) ||
        (width==4U
          ? seed_type.integer_rank != MINIC_INTEGER_RANK_INT
          : seed_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
            seed_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* GNU Linux PREFETCHW ALTERNATIVE: preserve the original 6.6.143
 * .altinstructions plus .altinstr_replacement ELF patching contract,
 * including both fallback and replacement native machine opcodes. */
/* x86 Linux 6.6 native APIC MMIO and AMD divider-clear CPU alternatives.
 * The source's ELF alternatives are NOT equivalent to executing either
 * machine instruction unconditionally: the first is fallback code, and
 * the second lives only in .altinstr_replacement. Match the entire GNU
 * alternative (feature keys, padding, sections, labels) without accepting
 * a nearby but semantically different asm template. */
unsigned int minic_x86_64_linux_apic_amd_alt_form(const char *s,size_t len) {
    static const struct { const char *first,*replacement,*feature; } modes[] = {
        {"661:movl%0,%P1662:", "6641:xchgl%0,%P16651:.popsection", "(22*32+(5))"},
        {"661:662:", "6641:div%26651:.popsection", "(22*32+(1*32+1))"}
    };
    static const char body[] =
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-."
        ".4byte%s"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\"";
    char compact[1024],expected[1024];size_t i,n=0U;
    if(!s||!len||len>=sizeof(compact))return 0U;
    for(i=0U;i<len;++i){
        const char ch=s[i];
        if(ch=='#'){while(i<len&&s[i]!='\n')++i;continue;}
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=ch;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(modes)/sizeof(modes[0]);++i){
        char metadata[800];
        const int metadata_count=snprintf(metadata,sizeof(metadata),body,
                                           modes[i].feature);
        const int count=metadata_count>0&&(size_t)metadata_count<sizeof(metadata)
            ? snprintf(expected,sizeof(expected),"%s%s%s",
                       modes[i].first,metadata,modes[i].replacement) : -1;
        if(count>0&&(size_t)count<sizeof(expected)&&
           strcmp(compact,expected)==0)return (unsigned int)(i+1U);
    }
    return 0U;
}

static bool x86_apic_amd_alt_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form=src?minic_x86_64_linux_apic_amd_alt_form(
        src->template_text,src->template_length):0U;
    size_t i;
    if(!program||!src||!contract||!form||!src->is_volatile||src->is_goto||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||src->input_count!=3U||!src->inputs||
       src->output_count!=(form==1U?2U:0U)||
       (form==1U&&!src->outputs))return false;
    if(form==1U){
        const MinicExpression *reg,*mem,*seed,*memory_input,*zero;
        if(!c_is(&src->outputs[0],"=r")||
           !c_is(&src->outputs[1],"=m")||
           !c_is(&src->inputs[0],"i")||
           !c_is(&src->inputs[1],"0")||
           !c_is(&src->inputs[2],"m"))return false;
        reg=minic_c0_program_expression(program,src->outputs[0].expression);
        mem=minic_c0_program_expression(program,src->outputs[1].expression);
        zero=minic_c0_program_expression(program,src->inputs[0].expression);
        seed=minic_c0_program_expression(program,src->inputs[1].expression);
        memory_input=minic_c0_program_expression(program,src->inputs[2].expression);
        if(!reg||!mem||!seed||!zero||!memory_input||
           reg->value_category!=MINIC_VALUE_LVALUE||
           mem->value_category!=MINIC_VALUE_LVALUE||
           memory_input->value_category!=MINIC_VALUE_LVALUE||
           minic_type_is_const(reg->type)||minic_type_is_const(mem->type)||
           !minic_type_is_integer(reg->type)||
           reg->type.integer_rank!=MINIC_INTEGER_RANK_INT||
           !minic_type_is_integer(mem->type)||
           mem->type.integer_rank!=MINIC_INTEGER_RANK_INT||
           !minic_type_equal(reg->type,seed->type)||
           !minic_type_equal(mem->type,memory_input->type)||
           zero->kind!=MINIC_EXPRESSION_INTEGER||
           zero->value.integer_value!=0)return false;
        contract->operand_count=5U;
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[4]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->implicit_memory_effect=true;
        return true;
    }
    if(!c_is(&src->inputs[0],"a")||
       !c_is(&src->inputs[1],"d")||
       !c_is(&src->inputs[2],"r"))return false;
    for(i=0U;i<3U;++i){
        const MinicExpression *e=minic_c0_program_expression(
            program,src->inputs[i].expression);
        if(!e||e->kind!=MINIC_EXPRESSION_INTEGER||
           e->value.integer_value!=(i==2U?1:0))return false;
    }
    contract->operand_count=3U;
    for(i=0U;i<3U;++i)
        contract->kinds[i]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

bool minic_x86_64_linux_prefetchw_alt_form(const char *s,size_t length) {
    static const char expected[]=
        "661:prefetcht0%P1662:"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-."
        ".4byte(6*32+8)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "6641:prefetchw%P16651:.popsection";
    char text[1024];size_t i,n=0U;
    if(!s||!length||length>=sizeof(text))return false;
    for(i=0U;i<length;++i) {
        const char c=s[i];
        if(c=='#'){while(i<length&&s[i]!='\n')++i;continue;}
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||n+1U>=sizeof(text))return false;
        text[n++]=c;
    }
    text[n]='\0';
    return strcmp(text,expected)==0;
}
static bool x86_prefetchw_alt_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *imm,*memory;
    if(!program||!src||!contract||!src->is_volatile||src->is_goto||
       src->output_count!=0U||src->input_count!=2U||src->inputs==NULL||
       src->label_count||src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||
       !minic_x86_64_linux_prefetchw_alt_form(
           src->template_text,src->template_length)||
       !c_is(&src->inputs[0],"i")||!c_is(&src->inputs[1],"m"))
        return false;
    imm=minic_c0_program_expression(program,src->inputs[0].expression);
    memory=minic_c0_program_expression(program,src->inputs[1].expression);
    if(!imm||imm->kind!=MINIC_EXPRESSION_INTEGER||
       imm->value.integer_value!=0||
       !memory||memory->value_category!=MINIC_VALUE_LVALUE)
        return false;
    contract->operand_count=2U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    return true;
}

/* Original Linux checksum_64.h add32_with_carry GNU tied "0" input
 * and second register-or-memory operand. Same shared arithmetic effect
 * family as csum_fold, but a DIFFERENT exact GNU constraint contract:
 *   =r output, 0 initial, rm added; ADD then ADC with live CF. */
bool minic_x86_64_add32_carry_template(const char *s,size_t len){
    static const char text[]="addl%2,%0adcl$0,%0";
    char norm[80];size_t i,n=0U;
    if(!s||!len||len>=sizeof(norm))return false;
    for(i=0U;i<len;++i){
        const char c=s[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(c==';'||n+1U>=sizeof(norm))return false;
        norm[n++]=c;
    }
    norm[n]=0;
    return strcmp(norm,text)==0;
}

/* TCP/UDP pseudoheader addition requires the complete carry chain, not four
 * independent additions. This is a distinct GNU contract from csum_add:
 * three g inputs and a tied fourth input initializing the output register. */
bool minic_x86_64_linux_tcpudp_nofold_template(const char *s, size_t len) {
    return x86_linux_compact_asm_equals(
        s, len, "addl%1,%0adcl%2,%0adcl%3,%0adcl$0,%0");
}

static bool x86_linux_tcpudp_nofold_source(
    const MinicC0Program *p, const MinicInlineAsm *s,
    MinicTargetInlineAsmContract *c) {
    size_t i;
    if (!p || !s || !c || s->is_volatile || s->is_goto ||
        s->has_memory_clobber || s->label_count || s->clobber_count ||
        s->register_clobber_count || s->output_count != 1U ||
        s->input_count != 4U || !s->outputs || !s->inputs ||
        !minic_x86_64_linux_tcpudp_nofold_template(
            s->template_text, s->template_length) ||
        !target_constraint_is(&s->outputs[0], "=r") ||
        !target_constraint_is(&s->inputs[0], "g") ||
        !target_constraint_is(&s->inputs[1], "g") ||
        !target_constraint_is(&s->inputs[2], "g") ||
        !target_constraint_is(&s->inputs[3], "0")) return false;
    for (i = 0U; i < 5U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U ?
            &s->outputs[0] : &s->inputs[i - 1U];
        const MinicExpression *expr = minic_c0_program_expression(p, op->expression);
        MinicType plain;
        if (!expr || !minic_type_unqualified(expr->type, &plain) ||
            plain.base_kind != MINIC_TYPE_BASE_INT ||
            plain.integer_rank != MINIC_INTEGER_RANK_INT ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type)))) return false;
    }
    c->operand_count = 5U;
    c->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 1U; i < 5U; ++i)
        c->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
static bool x86_add32_carry_source(const MinicC0Program *p,
    const MinicInlineAsm *s,MinicTargetInlineAsmContract *c){
    size_t i;
    if(!p||!s||!c||s->is_volatile||s->is_goto||
       s->has_memory_clobber||s->label_count||s->clobber_count||
       s->register_clobber_count||s->output_count!=1U||
       s->input_count!=2U||!s->outputs||!s->inputs||
       !minic_x86_64_add32_carry_template(
            s->template_text,s->template_length)||
       !c_is(&s->outputs[0],"=r")||
       !c_is(&s->inputs[0],"0")||!c_is(&s->inputs[1],"rm"))
        return false;
    for(i=0U;i<3U;++i){
        const MinicInlineAsmOperand *op=i==0U?
            &s->outputs[0]:&s->inputs[i-1U];
        const MinicExpression *expr=minic_c0_program_expression(
            p,op->expression);
        MinicType type;
        if(!expr||!minic_type_unqualified(expr->type,&type)||
           type.base_kind!=MINIC_TYPE_BASE_INT||
           type.integer_rank!=MINIC_INTEGER_RANK_INT||
           (i==0U&&(expr->value_category!=MINIC_VALUE_LVALUE||
                     minic_type_is_const(expr->type))))
            return false;
    }
    c->operand_count=3U;
    c->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    c->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    c->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_nospec_source(const MinicC0Program *program,
                              const MinicInlineAsm *src) {
    const MinicExpression *out, *size, *index;
    MinicType out_type, size_type, index_type;
    if (!program || !src || !src->outputs || !src->inputs ||
        !src->is_volatile || src->is_goto || src->label_count ||
        src->output_count != 1U || src->input_count != 2U ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        !src->register_clobbers || src->has_memory_clobber ||
        !clobber_is(&src->register_clobbers[0], "cc") ||
        !minic_x86_64_asm_nospec_template(src->template_text, src->template_length) ||
        !c_is(&src->outputs[0], "=r") ||
        !(c_is(&src->inputs[0], "g") || c_is(&src->inputs[0], "rm") ||
          c_is(&src->inputs[0], "r")) ||
        !c_is(&src->inputs[1], "r"))
        return false;
    out = minic_c0_program_expression(program, src->outputs[0].expression);
    size = minic_c0_program_expression(program, src->inputs[0].expression);
    index = minic_c0_program_expression(program, src->inputs[1].expression);
    return out && size && index &&
        out->value_category == MINIC_VALUE_LVALUE &&
        !minic_type_is_const(out->type) &&
        minic_type_unqualified(out->type, &out_type) &&
        minic_type_equal(out_type, minic_type_unsigned_long()) &&
        minic_type_unqualified(size->type, &size_type) &&
        minic_type_unqualified(index->type, &index_type) &&
        minic_type_equal(size_type, index_type) &&
        size_type.base_kind == MINIC_TYPE_BASE_INT &&
        (size_type.integer_rank == MINIC_INTEGER_RANK_INT ||
         size_type.integer_rank == MINIC_INTEGER_RANK_LONG ||
         size_type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG);
}

bool minic_x86_64_asm_mulacc_template(const char *template_text,
                                       size_t template_length) {
    static const char expected[] = "mulq%7addq%%rax,%0adcq%%rdx,%1adcq$0,%2";
    char text[128];
    size_t i, j = 0U;
    if (template_length >= sizeof(text)) return false;
    for (i = 0U; i < template_length; ++i) {
        const char c = template_text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j >= sizeof(text)-1U) return false;
        text[j++] = c;
    }
    text[j] = '\0';
    return strcmp(text, expected) == 0;
}

/* Original Linux mul_u64_u64_div_u64:
 *   asm("mulq %2; divq %3" : "=a"(q)
 *       : "a"(a), "rm"(mul), "rm"(div) : "rdx");
 * Admit one exact machine sequence, never generic executable template
 * passthrough with unchecked physical clobbers. The machine emitter chooses
 * the register alternative for both rm inputs and keeps RDX out of inputs. */
bool minic_x86_64_asm_muldiv_template(const char *text, size_t length) {
    static const char expected[] = "mulq%2;divq%3";
    char normalized[64];
    size_t i, j = 0U;
    if (!text || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j + 1U >= sizeof(normalized)) return false;
        normalized[j++] = c;
    }
    normalized[j] = '\0';
    return strcmp(normalized, expected) == 0;
}

/* Exact original GNU Linux __raw_try_cmpxchg 8-case source.
 * Linux CC_SET(z) must remain a real hardware flag output, not C logic. */
static const MinicX86TryCasDesc x86_trycas_forms[] = {
    {"cmpxchgb","%r10b","movzbl (%r11), %eax","movb %al, (%r11)",1U,true},
    {"cmpxchgw","%r10w","movzwl (%r11), %eax","movw %ax, (%r11)",2U,true},
    {"cmpxchgl","%r10d","movl (%r11), %eax","movl %eax, (%r11)",4U,true},
    {"cmpxchgq","%r10","movq (%r11), %rax","movq %rax, (%r11)",8U,true},
    {"cmpxchgb","%r10b","movzbl (%r11), %eax","movb %al, (%r11)",1U,false},
    {"cmpxchgw","%r10w","movzwl (%r11), %eax","movw %ax, (%r11)",2U,false},
    {"cmpxchgl","%r10d","movl (%r11), %eax","movl %eax, (%r11)",4U,false},
    {"cmpxchgq","%r10","movq (%r11), %rax","movq %rax, (%r11)",8U,false}
};
const MinicX86TryCasDesc *minic_x86_64_trycas_desc(MinicX86TryCas form) {
    return form>=MINIC_X86_TRYCAS_B_LOCK&&form<=MINIC_X86_TRYCAS_Q_LOCAL ?
        &x86_trycas_forms[(size_t)form-1U]:NULL;
}
MinicX86TryCas minic_x86_64_trycas_form(const char *text,size_t len) {
    static const char smp[]=
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256],expected[256];
    size_t i,n=0U,mode,variant;
    if(!text||!len||len>=sizeof(compact))return MINIC_X86_TRYCAS_NONE;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\n'||c=='\r')continue;
        if(n+1U>=sizeof(compact))return MINIC_X86_TRYCAS_NONE;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(mode=0U;mode<sizeof(x86_trycas_forms)/sizeof(x86_trycas_forms[0]);++mode)
        for(variant=0U;variant<2U;++variant) {
            const MinicX86TryCasDesc *d=&x86_trycas_forms[mode];
            const int written=snprintf(expected,sizeof(expected),
                "%s%s%s,%s/*outputconditioncodez*/",
                d->locked?smp:"",d->op,
                variant?"%3":"%[new]",variant?"%1":"%[ptr]");
            if(written>0&&(size_t)written<sizeof(expected)&&
               strcmp(compact,expected)==0)
                return (MinicX86TryCas)(mode+1U);
        }
    return MINIC_X86_TRYCAS_NONE;
}

/* Genuine Linux 6.6.143 arch_cmpxchg (not try_cmpxchg):
 * .smp_locks metadata; lock cmpxchgb/w/l/q %2,%1;
 * outputs =a and +m, input new q/r, tied old "0". One physical ISA
 * descriptor shared with the pre-existing flag-return trycas family. */
MinicX86TryCas minic_x86_64_raw_cmpxchg_form(
    const char *text, size_t length) {
    static const char smp[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i, n = 0U, form;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return MINIC_X86_TRYCAS_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_TRYCAS_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (form = 0U; form < sizeof(x86_trycas_forms) /
                            sizeof(x86_trycas_forms[0]); ++form) {
        const MinicX86TryCasDesc *d = &x86_trycas_forms[form];
        int written = snprintf(expected, sizeof(expected), "%s%s%%2,%%1",
                               d->locked ? smp : "", d->op);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(expected, compact) == 0)
            return (MinicX86TryCas)(form + 1U);
    }
    return MINIC_X86_TRYCAS_NONE;
}
static bool x86_linux_raw_cmpxchg_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86TryCas form = src ? minic_x86_64_raw_cmpxchg_form(
        src->template_text, src->template_length) : MINIC_X86_TRYCAS_NONE;
    const MinicX86TryCasDesc *d = minic_x86_64_trycas_desc(form);
    size_t i;
    if (!program || !src || !contract || !d || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 2U || src->input_count != 2U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U || src->label_count != 0U ||
        !target_constraint_is(&src->outputs[0], "=a") ||
        !target_constraint_is(&src->outputs[1], "+m") ||
        !target_constraint_is(&src->inputs[0], d->width == 1U ? "q" : "r") ||
        !target_constraint_is(&src->inputs[1], "0"))
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i < 2U
            ? &src->outputs[i] : &src->inputs[i - 2U];
        const MinicExpression *e = minic_c0_program_expression(
            program, op->expression);
        MinicType type;
        const unsigned int rank = d->width == 1U ? MINIC_INTEGER_RANK_CHAR :
            d->width == 2U ? MINIC_INTEGER_RANK_SHORT :
            d->width == 4U ? MINIC_INTEGER_RANK_INT : MINIC_INTEGER_RANK_NONE;
        if (e == NULL || (i < 2U &&
            (e->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(e->type))) ||
            !minic_type_unqualified(e->type, &type) ||
            (!(d->width == 8U && minic_type_is_pointer(type)) &&
             (type.base_kind != MINIC_TYPE_BASE_INT ||
             !minic_type_is_integer(type) || type.pointer_depth != 0U ||
             (d->width == 8U
                 ? (type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                    type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)
                 : (unsigned int)type.integer_rank != rank))))
            return false;
    }
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* Original Linux asm/shared/io.h: target-owned 1/2/4 byte privileged port
 * input and output. Unknown suffixes never flow into the GNU assembler. */
static const MinicX86PortIoDesc x86_port_io[]={
    {"outb",1U,false},{"outw",2U,false},{"outl",4U,false},
    {"inb",1U,true},{"inw",2U,true},{"inl",4U,true}
};
const MinicX86PortIoDesc *minic_x86_64_port_io_description(
    MinicX86PortIo form){
    const size_t n=(size_t)form;
    return n>0U&&n<=sizeof(x86_port_io)/sizeof(x86_port_io[0])?
        &x86_port_io[n-1U]:NULL;
}
MinicX86PortIo minic_x86_64_port_io_form(const char *s,size_t len){
    static const char *const names[]={
        "outb%b0,%w1","outw%w0,%w1","outl%0,%w1",
        "inb%w1,%b0","inw%w1,%w0","inl%w1,%0"
    };
    char compact[64];size_t i,j=0U;
    if(!s||!len||len>=sizeof(compact))return MINIC_X86_PORT_IO_NONE;
    for(i=0U;i<len;++i){
        const char c=s[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_PORT_IO_NONE;
        compact[j++]=c;
    }
    compact[j]=0;
    for(i=0U;i<sizeof(names)/sizeof(names[0]);++i)
        if(strcmp(compact,names[i])==0)return (MinicX86PortIo)(i+1U);
    return MINIC_X86_PORT_IO_NONE;
}
static bool x86_port_io_source(const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract){
    const MinicX86PortIo form=src?minic_x86_64_port_io_form(
        src->template_text,src->template_length):MINIC_X86_PORT_IO_NONE;
    const MinicX86PortIoDesc *d=minic_x86_64_port_io_description(form);
    const MinicExpression *data,*port;
    MinicType dt,pt;
    if(!program||!src||!contract||!d||!src->is_volatile||src->is_goto||
       src->clobber_count||src->register_clobber_count||
       src->has_memory_clobber||src->label_count||
       src->output_count!=(d->is_input?1U:0U)||
       src->input_count!=(d->is_input?1U:2U)||
       !src->inputs||(d->is_input&&!src->outputs))return false;
    if(d->is_input){
        if(!target_constraint_is(&src->outputs[0],"=a")||
           !target_constraint_is(&src->inputs[0],"Nd"))return false;
        data=minic_c0_program_expression(program,src->outputs[0].expression);
        port=minic_c0_program_expression(program,src->inputs[0].expression);
        if(!data||data->value_category!=MINIC_VALUE_LVALUE||
           minic_type_is_const(data->type))return false;
    }else{
        if(!target_constraint_is(&src->inputs[0],"a")||
           !target_constraint_is(&src->inputs[1],"Nd"))return false;
        data=minic_c0_program_expression(program,src->inputs[0].expression);
        port=minic_c0_program_expression(program,src->inputs[1].expression);
    }
    if(!data||!port||!minic_type_unqualified(data->type,&dt)||
       !minic_type_unqualified(port->type,&pt)||
       !minic_type_equal(pt,minic_type_unsigned_short())||
       !(d->width==1U?minic_type_equal(dt,minic_type_unsigned_char()):
         d->width==2U?minic_type_equal(dt,minic_type_unsigned_short()):
                      minic_type_equal(dt,minic_type_unsigned_int())))return false;
    contract->operand_count=2U;
    contract->kinds[0]=d->is_input?
       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
       MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_trycas_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86TryCas form=src ?
        minic_x86_64_trycas_form(src->template_text,src->template_length):
        MINIC_X86_TRYCAS_NONE;
    const MinicX86TryCasDesc *d=minic_x86_64_trycas_desc(form);
    const MinicExpression *e[4];
    MinicType t[4];
    const unsigned int rank=d ?
        d->width==1U?MINIC_INTEGER_RANK_CHAR:
        d->width==2U?MINIC_INTEGER_RANK_SHORT:
        d->width==4U?MINIC_INTEGER_RANK_INT:MINIC_INTEGER_RANK_LONG_LONG:
        MINIC_INTEGER_RANK_NONE;
    size_t i;
    if(!program||!src||!contract||!d||!src->is_volatile||src->is_goto||
       !src->outputs||!src->inputs||src->output_count!=3U||
       src->input_count!=1U||src->label_count||!src->has_memory_clobber||
       src->clobber_count!=1U||src->register_clobber_count||
       !target_constraint_is(&src->outputs[0],"=@ccz")||
       !target_constraint_is(&src->outputs[1],"+m")||
       !target_constraint_is(&src->outputs[2],"+a")||
       !target_constraint_is(&src->inputs[0],d->width==1U?"q":"r"))
        return false;
    for(i=0U;i<4U;++i){
        const MinicInlineAsmOperand *op=i<3U?
            &src->outputs[i]:&src->inputs[0];
        e[i]=minic_c0_program_expression(program,op->expression);
        if(!e[i]||(i<3U&&(e[i]->value_category!=MINIC_VALUE_LVALUE||
                             minic_type_is_const(e[i]->type)))||
           !minic_type_unqualified(e[i]->type,&t[i]))
            return false;
    }
    /* Original Linux __raw_try_cmpxchg casts the memory carrier to
     * volatile u{8,16,32,64}, while __old/__new retain the source
     * operand's possibly SIGNED C type (e.g. atomic_t.counter is int).
     * CMPXCHG compares and exchanges bit patterns of an exact width; the
     * source signedness is not a register/ISA semantic constraint.
     * Admit only real primitive integer types of the SAME machine rank,
     * never bool, enum, pointer or widened/narrowed value operands.
     * This also matches the existing machine emitter's independent width
     * verification; no raw GNU template fallback is introduced. */
    if(!minic_type_equal(t[0],minic_type_bool()))
        return false;
    for(i=1U;i<4U;++i)
        if (!(d->width == 8U && minic_type_is_pointer(t[i])) &&
            (t[i].base_kind!=MINIC_TYPE_BASE_INT||
             !minic_type_is_integer(t[i])||t[i].pointer_depth!=0U||
             (d->width == 8U
                 ? (t[i].integer_rank != MINIC_INTEGER_RANK_LONG &&
                    t[i].integer_rank != MINIC_INTEGER_RANK_LONG_LONG)
                 : (unsigned int)t[i].integer_rank!=rank)))
            return false;
    contract->operand_count=4U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[3]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}


/* Original Linux 6.6.x x86 atomic RMW: one ISA owner for binary
 * ADD/SUB/AND/OR/XOR (32/64, SMP/local), unary INC/DEC (32/64), and
 * the 64-bit =m output + separate m-input form of ADD/SUB/INC/DEC.
 * The latter is a REAL hardware read/write even though GNU spells the
 * output write-only; the explicit m-input is an ordering dependency. */
static const MinicX86AtomicRmwDesc x86_linux_atomic_rmw[] = {
    {"addl",4U,false,false}, {"subl",4U,false,false},
    {"andl",4U,false,false}, {"orl",4U,false,false},
    {"xorl",4U,false,false},
    {"addq",8U,false,false}, {"subq",8U,false,false},
    {"andq",8U,false,false}, {"orq",8U,false,false},
    {"xorq",8U,false,false},
    {"addl",4U,true,false}, {"subl",4U,true,false},
    {"andl",4U,true,false}, {"orl",4U,true,false},
    {"xorl",4U,true,false},
    {"addq",8U,true,false}, {"subq",8U,true,false},
    {"andq",8U,true,false}, {"orq",8U,true,false},
    {"xorq",8U,true,false},
    {"incl",4U,true,true}, {"decl",4U,true,true},
    {"incq",8U,true,true}, {"decq",8U,true,true},
    {"incl",4U,false,true}, {"decl",4U,false,true},
    {"incq",8U,false,true}, {"decq",8U,false,true}
};
const MinicX86AtomicRmwDesc *minic_x86_64_atomic_rmw_description(
    MinicX86AtomicRmw form) {
    const size_t index=(size_t)form;
    return index>0U &&
           index<=sizeof(x86_linux_atomic_rmw)/sizeof(x86_linux_atomic_rmw[0])
        ? &x86_linux_atomic_rmw[index-1U] : NULL;
}
MinicX86AtomicRmw minic_x86_64_atomic_rmw_form(const char *text,size_t len) {
    static const char smp[]=
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i,j=0U,k,variant;
    if(!text||!len||len>=sizeof(compact))return MINIC_X86_ATOMIC_RMW_NONE;
    for(i=0U;i<len;++i){
        const char c=text[i];
        if(c==' '||c==9||c==10||c==13)continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_ATOMIC_RMW_NONE;
        compact[j++]=c;
    }
    compact[j]=0;
    for(k=0U;k<sizeof(x86_linux_atomic_rmw)/
                    sizeof(x86_linux_atomic_rmw[0]);++k){
        const MinicX86AtomicRmwDesc *d=&x86_linux_atomic_rmw[k];
        for(variant=0U;variant<2U;++variant){
            const int n=d->unary?
                snprintf(expected,sizeof(expected),"%s%s%s",
                    d->locked?smp:"",d->opcode,variant?"%[var]":"%0"):
                snprintf(expected,sizeof(expected),"%s%s%s,%s",
                    d->locked?smp:"",d->opcode,
                    variant?"%[val]":"%1",variant?"%[var]":"%0");
            if(n>0&&(size_t)n<sizeof(expected) &&
               strcmp(compact,expected)==0)
                return (MinicX86AtomicRmw)(k+1U);
        }
    }
    return MINIC_X86_ATOMIC_RMW_NONE;
}
static bool x86_linux_atomic_rmw_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86AtomicRmw form=src?
        minic_x86_64_atomic_rmw_form(src->template_text,src->template_length):
        MINIC_X86_ATOMIC_RMW_NONE;
    const MinicX86AtomicRmwDesc *d=
        minic_x86_64_atomic_rmw_description(form);
    const MinicExpression *mem,*value=NULL,*dependency=NULL;
    MinicType mt,vt,dependency_type;
    size_t width;
    bool legacy,unary;
    if(!program||!src||!contract||!d||!src->is_volatile||src->is_goto||
       src->output_count!=1U||!src->outputs||src->label_count||
       (src->has_memory_clobber ? src->clobber_count!=1U :
        (src->clobber_count!=0U || d->locked))||
       src->register_clobber_count)return false;
    unary=d->unary;
    legacy=target_constraint_is(&src->outputs[0],"=m");
    if(legacy &&
       (!d->locked||d->width!=8U||
        (!unary && strcmp(d->opcode,"addq")!=0 &&
                   strcmp(d->opcode,"subq")!=0)))return false;
    if(!legacy && !target_constraint_is(&src->outputs[0],"+m"))
        return false;
    if(src->input_count!=(unary?0U:1U)+(legacy?1U:0U) ||
       (src->input_count&&!src->inputs))return false;
    if(!unary){
        if(!(target_constraint_is(&src->inputs[0],"ir")||
             target_constraint_is(&src->inputs[0],"er")||
             target_constraint_is(&src->inputs[0],"r")))return false;
        value=minic_c0_program_expression(program,src->inputs[0].expression);
    }
    if(legacy){
        const MinicInlineAsmOperand *op=&src->inputs[unary?0U:1U];
        if(!target_constraint_is(op,"m"))return false;
        dependency=minic_c0_program_expression(program,op->expression);
        if(!dependency||dependency->value_category!=MINIC_VALUE_LVALUE||
           !minic_type_unqualified(dependency->type,&dependency_type))
            return false;
    }
    mem=minic_c0_program_expression(program,src->outputs[0].expression);
    if(!mem||mem->value_category!=MINIC_VALUE_LVALUE||
       minic_type_is_const(mem->type)||
       !minic_type_unqualified(mem->type,&mt)||
       mt.base_kind!=MINIC_TYPE_BASE_INT||
       !minic_type_is_integer(mt)||mt.pointer_depth)return false;
    width=mt.integer_rank==MINIC_INTEGER_RANK_INT?4U:
          (mt.integer_rank==MINIC_INTEGER_RANK_LONG ||
           mt.integer_rank==MINIC_INTEGER_RANK_LONG_LONG?8U:0U);
    if(width!=d->width)return false;
    /* Original x86 Linux local.h: local_add(long i) uses LONG input into
     * atomic_long_t=atomic64_t with LONG_LONG counter. Distinct C ranks
     * are the same 64-bit x86 hardware register/memory operand, and the
     * machine emitter already checks exact physical width. */
    if(value &&
       (!minic_type_unqualified(value->type,&vt)||
        vt.base_kind!=MINIC_TYPE_BASE_INT||
        !minic_type_is_integer(vt)||vt.pointer_depth||
        (d->width==8U
           ? (vt.integer_rank!=MINIC_INTEGER_RANK_LONG &&
              vt.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)
           : vt.integer_rank!=MINIC_INTEGER_RANK_INT)))return false;
    if(legacy &&
       (dependency_type.base_kind!=MINIC_TYPE_BASE_INT||
        !minic_type_is_integer(dependency_type)||
        dependency_type.pointer_depth||
        (d->width==8U
           ? (dependency_type.integer_rank!=MINIC_INTEGER_RANK_LONG &&
              dependency_type.integer_rank!=MINIC_INTEGER_RANK_LONG_LONG)
           : dependency_type.integer_rank!=MINIC_INTEGER_RANK_INT)))
        return false;
    contract->operand_count=1U+(unary?0U:1U)+(legacy?1U:0U);
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    if(!unary)contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if(legacy)contract->kinds[unary?1U:2U]=
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    return true;
}

/* Original GNU Linux SMP xadd (1/2/4/8): 671 patch section plus LOCK.
 * This ABI/ISA contract is separate from non-LOCK local_add_return. */
size_t minic_x86_64_linux_smp_xadd_width(const char *text,size_t len) {
    static const char *const forms[] = {
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddb%b0,%1",
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddw%w0,%1",
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddl%0,%1",
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:"
        "lock;xaddq%q0,%1"
    };
    char compact[256]; size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return 0U;
    for(i=0U;i<len;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\t'||c=='\r')continue;
        if(n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<4U;++i)
        if(strcmp(compact,forms[i])==0)return (size_t)1U<<i;
    return 0U;
}
static bool x86_linux_smp_xadd_source(const MinicC0Program *program,
    const MinicInlineAsm *src,MinicTargetInlineAsmContract *contract) {
    const size_t width=src?minic_x86_64_linux_smp_xadd_width(
        src->template_text,src->template_length):0U;
    const MinicExpression *reg,*mem;
    MinicType rt,mt;
    const MinicIntegerRank rank=width==1U?MINIC_INTEGER_RANK_CHAR:
        width==2U?MINIC_INTEGER_RANK_SHORT:
        width==4U?MINIC_INTEGER_RANK_INT:MINIC_INTEGER_RANK_LONG;
    if(!program||!src||!contract||!width||!src->is_volatile||src->is_goto||
        !src->outputs||src->output_count!=2U||src->input_count||
        src->label_count||!src->has_memory_clobber||src->clobber_count!=2U||
        src->register_clobber_count!=1U||!src->register_clobbers||
        !clobber_is(&src->register_clobbers[0],"cc")||
        !c_is(&src->outputs[0],width==1U?"+q":"+r")||
        !c_is(&src->outputs[1],"+m"))return false;
    reg=minic_c0_program_expression(program,src->outputs[0].expression);
    mem=minic_c0_program_expression(program,src->outputs[1].expression);
    if(!reg||!mem||reg->value_category!=MINIC_VALUE_LVALUE||
        mem->value_category!=MINIC_VALUE_LVALUE||
        minic_type_is_const(reg->type)||minic_type_is_const(mem->type)||
        !minic_type_unqualified(reg->type,&rt)||
        !minic_type_unqualified(mem->type,&mt)||
        !minic_type_is_integer(rt)||!minic_type_is_integer(mt)||
        rt.base_kind!=MINIC_TYPE_BASE_INT||mt.base_kind!=MINIC_TYPE_BASE_INT||
        rt.pointer_depth||mt.pointer_depth||
        rt.integer_rank!=rank||mt.integer_rank!=rank)return false;
    contract->operand_count=2U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    return true;
}

/* Linux 6.6.143 arch/x86/include/asm/local.h local_add_return:
 * register +r receives the PREVIOUS memory value after bare xaddq; +m
 * is the true hardware RMW lvalue. Core uses existing typed operand roles.
 * This target-owned ISA contract must never widen into generic ASM text. */
size_t minic_x86_64_linux_local_xadd_width(const char *text, size_t len) {
    char compact[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, "xaddq%0,%1;") == 0) return 8U;
    if (strcmp(compact, "xaddl%0,%1;") == 0) return 4U;
    return 0U;
}
static bool x86_linux_local_xadd_source(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *reg, *mem;
    MinicType reg_type, mem_type;
    const size_t width = src ? minic_x86_64_linux_local_xadd_width(
        src->template_text, src->template_length) : 0U;
    if (!program || !src || !contract || !width ||
        !src->is_volatile || src->is_goto ||
        !src->outputs || src->output_count != 2U ||
        src->input_count != 0U || src->label_count != 0U ||
        !src->has_memory_clobber || src->clobber_count != 1U ||
        src->register_clobber_count != 0U ||
        !target_constraint_is(&src->outputs[0], "+r") ||
        !target_constraint_is(&src->outputs[1], "+m")) return false;
    reg = minic_c0_program_expression(program, src->outputs[0].expression);
    mem = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!reg || !mem ||
        reg->value_category != MINIC_VALUE_LVALUE ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(reg->type) || minic_type_is_const(mem->type) ||
        !minic_type_unqualified(reg->type, &reg_type) ||
        !minic_type_unqualified(mem->type, &mem_type) ||
        reg_type.base_kind != MINIC_TYPE_BASE_INT ||
        mem_type.base_kind != MINIC_TYPE_BASE_INT ||
        !minic_type_is_integer(reg_type) || !minic_type_is_integer(mem_type) ||
        reg_type.pointer_depth || mem_type.pointer_depth ||
        reg_type.integer_rank != mem_type.integer_rank ||
        (width == 8U
         ? (reg_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
            reg_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)
         : reg_type.integer_rank != MINIC_INTEGER_RANK_INT))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    return true;
}

/* XADD against GS-relative per-CPU memory returns the previous value in the
 * register lvalue and updates the per-CPU slot, exactly as the direct-memory
 * Linux local XADD does. The GS segment belongs solely to this x86 target. */
size_t minic_x86_64_linux_percpu_xadd_width(const char *text, size_t length) {
    static const char *const patterns[] = {
        "xaddb%0,%%gs:%1", "xaddw%0,%%gs:%1",
        "xaddl%0,%%gs:%1", "xaddq%0,%%gs:%1",
        "xaddb%[tmp],%%gs:%[var]", "xaddw%[tmp],%%gs:%[var]",
        "xaddl%[tmp],%%gs:%[var]", "xaddq%[tmp],%%gs:%[var]"
    };
    static const size_t widths[] = {1U, 2U, 4U, 8U, 1U, 2U, 4U, 8U};
    char compact[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(widths)/sizeof(widths[0]); ++i)
        if (strcmp(compact, patterns[i]) == 0) return widths[i];
    return 0U;
}

static bool x86_linux_percpu_xadd_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *reg, *memory;
    MinicType reg_type, mem_type;
    const size_t width = source ? minic_x86_64_linux_percpu_xadd_width(
        source->template_text, source->template_length) : 0U;
    if (!program || !source || !contract || width == 0U ||
        source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count || source->label_count ||
        source->register_clobber_count ||
        source->clobber_count != (source->has_memory_clobber ? 1U : 0U) ||
        !(target_constraint_is(&source->outputs[0], "+r") ||
          (width == 1U && target_constraint_is(&source->outputs[0], "+q"))) ||
        !target_constraint_is(&source->outputs[1], "+m")) return false;
    reg = minic_c0_program_expression(program, source->outputs[0].expression);
    memory = minic_c0_program_expression(program, source->outputs[1].expression);
    if (!reg || !memory ||
        reg->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(reg->type) || minic_type_is_const(memory->type) ||
        !minic_type_unqualified(reg->type, &reg_type) ||
        !minic_type_unqualified(memory->type, &mem_type) ||
        !minic_type_is_integer(reg_type) ||
        !minic_type_is_integer(mem_type) ||
        (width == 1U && reg_type.integer_rank != MINIC_INTEGER_RANK_CHAR) ||
        (width == 2U && reg_type.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
        (width == 4U && reg_type.integer_rank != MINIC_INTEGER_RANK_INT) ||
        (width == 8U && reg_type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                         reg_type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

/* GNU __this_cpu_cmpxchg: the +a carrier starts with the expected value;
 * cmpxchg updates GS-relative memory only on equality and returns the
 * observed previous memory value in that same carrier. Keep the memory
 * operand separate from an ordinary C dereference: %gs is architectural. */
static size_t minic_x86_64_linux_gs_cmpxchg_loop_width(
    const char *text, size_t length);
size_t minic_x86_64_linux_gs_cmpxchg_width(const char *text, size_t length) {
    static const char *const symbolic[] = {
        "cmpxchgb%[nval],%%gs:%[var]", "cmpxchgw%[nval],%%gs:%[var]",
        "cmpxchgl%[nval],%%gs:%[var]", "cmpxchgq%[nval],%%gs:%[var]"
    };
    static const char *const numeric[] = {
        "cmpxchgb%2,%%gs:%1", "cmpxchgw%2,%%gs:%1",
        "cmpxchgl%2,%%gs:%1", "cmpxchgq%2,%%gs:%1"
    };
    static const size_t widths[] = {1U, 2U, 4U, 8U};
    size_t i;
    for (i = 0U; i < 4U; ++i)
        if (x86_linux_compact_asm_equals(text, length, symbolic[i]) ||
            x86_linux_compact_asm_equals(text, length, numeric[i]))
            return widths[i];
    return minic_x86_64_linux_gs_cmpxchg_loop_width(text, length);
}

static size_t minic_x86_64_linux_gs_cmpxchg_loop_width(
    const char *text, size_t length) {
    static const char *const opcodes[] = {
        "cmpxchgb", "cmpxchgw", "cmpxchgl", "cmpxchgq"
    };
    size_t i;
    for (i = 0U; i < 4U; ++i) {
        char expected[128];
        int n = snprintf(expected, sizeof(expected),
            "mov%c%%%%gs:%%[var],%%[oval]1:%s%%[nval],%%%%gs:%%[var]jnz1b",
            "bwlq"[i], opcodes[i]);
        if (n > 0 && (size_t)n < sizeof(expected) &&
            x86_linux_compact_asm_equals(text, length, expected))
            return (size_t)(1U << i);
        n = snprintf(expected, sizeof(expected),
            "mov%c%%%%gs:%%1,%%01:%s%%2,%%%%gs:%%1jnz1b",
            "bwlq"[i], opcodes[i]);
        if (n > 0 && (size_t)n < sizeof(expected) &&
            x86_linux_compact_asm_equals(text, length, expected))
            return (size_t)(1U << i);
    }
    return 0U;
}

bool minic_x86_64_linux_gs_cmpxchg_loop(const char *text, size_t length) {
    return minic_x86_64_linux_gs_cmpxchg_loop_width(text, length) != 0U;
}

static bool x86_linux_gs_cmpxchg_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const size_t width = source ? minic_x86_64_linux_gs_cmpxchg_width(
        source->template_text, source->template_length) : 0U;
    size_t i;
    const bool loop = source && minic_x86_64_linux_gs_cmpxchg_loop(
        source->template_text, source->template_length);
    if (!program || !source || !contract || !width || source->is_goto ||
        source->output_count != 2U || source->input_count != 1U ||
        !source->outputs || !source->inputs || source->label_count ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], loop ? "=&a" : "+a") ||
        !target_constraint_is(&source->outputs[1], "+m") ||
        !target_constraint_is(&source->inputs[0], width == 1U ? "q" : "r"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *operand = i < 2U
            ? &source->outputs[i] : &source->inputs[0];
        const MinicExpression *expression = minic_c0_program_expression(
            program, operand->expression);
        MinicType unqualified;
        unsigned int rank;
        if (!expression || (i < 2U &&
            (expression->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(expression->type))) ||
            !minic_type_unqualified(expression->type, &unqualified) ||
            !(minic_type_is_integer(unqualified) ||
              (i == 1U && width == 8U && minic_type_is_pointer(unqualified))) ||
            (width != 8U && unqualified.base_kind != MINIC_TYPE_BASE_INT))
            return false;
        if (width == 8U && i == 1U && minic_type_is_pointer(unqualified))
            continue;
        rank = width == 1U ? MINIC_INTEGER_RANK_CHAR :
               width == 2U ? MINIC_INTEGER_RANK_SHORT :
               width == 4U ? MINIC_INTEGER_RANK_INT :
               MINIC_INTEGER_RANK_LONG;
        if (unqualified.integer_rank != rank &&
            !(width == 8U && unqualified.integer_rank == MINIC_INTEGER_RANK_LONG_LONG))
            return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = loop
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux memset16/32/64: two early-clobbered fixed register results and
 * three GNU operands (A, tied D, tied C). A REP string operation mutates
 * both RDI and RCX, so a cosmetic "rep stos" emission is not sufficient. */
size_t minic_x86_64_linux_rep_stos_width(const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length, "repstosw")) return 2U;
    if (x86_linux_compact_asm_equals(text, length, "repstosl")) return 4U;
    if (x86_linux_compact_asm_equals(text, length, "repstosq")) return 8U;
    return 0U;
}

static bool x86_linux_rep_stos_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const size_t width = source ? minic_x86_64_linux_rep_stos_width(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *output_count, *output_dest, *value, *dest, *count;
    MinicType type, pointee;
    if (!program || !source || !contract || !width ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || source->input_count != 3U ||
        !source->outputs || !source->inputs || source->label_count ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=&c") ||
        !target_constraint_is(&source->outputs[1], "=&D") ||
        !target_constraint_is(&source->inputs[0], "a") ||
        !target_constraint_is(&source->inputs[1], "1") ||
        !target_constraint_is(&source->inputs[2], "0")) return false;
    output_count = minic_c0_program_expression(program, source->outputs[0].expression);
    output_dest = minic_c0_program_expression(program, source->outputs[1].expression);
    value = minic_c0_program_expression(program, source->inputs[0].expression);
    dest = minic_c0_program_expression(program, source->inputs[1].expression);
    count = minic_c0_program_expression(program, source->inputs[2].expression);
    if (!output_count || !output_dest || !value || !dest || !count ||
        output_count->value_category != MINIC_VALUE_LVALUE ||
        output_dest->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output_count->type) ||
        minic_type_is_const(output_dest->type) ||
        !minic_type_unqualified(value->type, &type) ||
        !minic_type_is_integer(type) ||
        (width == 2U && type.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
        (width == 4U && type.integer_rank != MINIC_INTEGER_RANK_INT) ||
        (width == 8U && type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                         type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG) ||
        !minic_type_pointee(dest->type, &pointee) ||
        !minic_type_is_integer(pointee) ||
        (width == 2U && pointee.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
        (width == 4U && pointee.integer_rank != MINIC_INTEGER_RANK_INT) ||
        (width == 8U && pointee.integer_rank != MINIC_INTEGER_RANK_LONG &&
                         pointee.integer_rank != MINIC_INTEGER_RANK_LONG_LONG) ||
        !minic_type_is_integer(count->type) ||
        !minic_type_is_integer(output_count->type) ||
        !minic_type_is_integer(output_dest->type))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* One exact x86 target table for independent Linux privileged/atomic and
 * non-temporal memory forms. Only instruction and GNU operand contracts are
 * arch-owned; Core continues carrying generic memory/scalar effects. */
unsigned int minic_x86_64_linux_lowlevel_scalar_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "hlt", "lldt%w0", "sldt%0", "mov%0,%%fs",
        "invpcid%[desc],%[type]", "btcq%1,%0",
        "movntil%1,%0", "movntiq%1,%0"
    };
    size_t i;
    for (i = 0U; i < sizeof(exact)/sizeof(exact[0]); ++i)
        if (x86_linux_compact_asm_equals(text, length, exact[i]))
            return (unsigned int)(i + 1U);
    if (x86_linux_compact_asm_equals(text, length, "invpcid%0,%1"))
        return 5U;
    return 0U;
}

static bool x86_linux_lowlevel_scalar_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_lowlevel_scalar_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *operand;
    MinicType plain;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count != (source->has_memory_clobber ? 1U : 0U))
        return false;
    if (form == 1U) {
        if (source->output_count || source->input_count) return false;
        contract->operand_count = 0U;
        contract->implicit_memory_effect = source->has_memory_clobber;
        return true;
    }
    if (form == 2U || form == 4U) {
        if (!source->is_volatile || source->output_count ||
            source->input_count != 1U || !source->inputs ||
            !target_constraint_is(&source->inputs[0], form == 2U ? "q" : "rm"))
            return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        return true;
    }
    if (form == 3U) {
        if (source->output_count != 1U || !source->outputs ||
            source->input_count ||
            !target_constraint_is(&source->outputs[0], "=m")) return false;
        operand = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(operand->type) ||
            !minic_type_unqualified(operand->type, &plain) ||
            plain.integer_rank != MINIC_INTEGER_RANK_SHORT ||
            !minic_type_is_integer(plain)) return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        contract->implicit_memory_effect = true;
        return true;
    }
    if (form == 5U) {
        if (!source->is_volatile || !source->has_memory_clobber ||
            source->output_count || source->input_count != 2U ||
            !source->inputs ||
            !target_constraint_is(&source->inputs[0], "m") ||
            !target_constraint_is(&source->inputs[1], "r")) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_record(operand->type)) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[1].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        return true;
    }
    if (form == 6U) {
        if (!source->is_volatile || !source->has_memory_clobber ||
            source->output_count || source->input_count != 2U ||
            !source->inputs ||
            !target_constraint_is(&source->inputs[0], "m") ||
            !target_constraint_is(&source->inputs[1], "Ir")) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(operand->type)) return false;
        operand = minic_c0_program_expression(
            program, source->inputs[1].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        return true;
    }
    if (form == 7U || form == 8U) {
        if (source->output_count != 1U || source->input_count != 1U ||
            !source->outputs || !source->inputs ||
            !target_constraint_is(&source->outputs[0], "=m") ||
            !target_constraint_is(&source->inputs[0], "r")) return false;
        operand = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!operand || operand->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(operand->type) ||
            !minic_type_unqualified(operand->type, &plain) ||
            !minic_type_is_integer(plain) ||
            (plain.integer_rank != (form == 7U ? MINIC_INTEGER_RANK_INT
                                                  : MINIC_INTEGER_RANK_LONG) &&
             !(form == 8U && plain.integer_rank == MINIC_INTEGER_RANK_LONG_LONG)))
            return false;
        operand = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!operand || !minic_type_is_integer(operand->type)) return false;
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        return true;
    }
    return false;
}

/* Complete compressed-boot cpuflags grammar: CF bit test, bit set, CPUID
 * register ties, and hardware EFLAGS probing. Do not silently lower these
 * architecture side effects as ordinary C integer operators. */
unsigned int minic_x86_64_linux_boot_flags_form(
    const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(
            text, length, "btl%2,%1/*outputconditioncodec*/")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "btsl%1,%0")) return 2U;
    if (x86_linux_compact_asm_equals(text, length, "cpuid")) return 3U;
    if (x86_linux_compact_asm_equals(text, length,
            "pushfqpushfqpop%0mov%0,%1xor%2,%1push%1popfq"
            "pushfqpop%1popfq")) return 4U;
    return 0U;
}

/* `mull %%edx` in x86 __const_udelay uses two tied implicit operands:
 * GNU input "1" supplies EAX, input "0" supplies EDX.  The 32x32 unsigned
 * multiply yields the high 32 bits in EDX, which are written to the 64-bit
 * delay counter, and the low bits in the early-clobber EAX scratch output. */
bool minic_x86_64_linux_delay_mul_form(const char *text, size_t length) {
    return text && length == sizeof("mull %%edx") - 1U &&
           memcmp(text, "mull %%edx", length) == 0;
}

unsigned int minic_x86_64_linux_delay_wait_form(
    const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length,
        "test%0,%0jz3fjmp1f.align161:jmp2f.align16"
        "2:dec%0jnz2b3:dec%0")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "tpause%%ecx")) return 2U;
    return 0U;
}

static bool x86_linux_delay_wait_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const tpause_constraints[] = {"c", "d", "a"};
    const unsigned int form = source ? minic_x86_64_linux_delay_wait_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *expr;
    size_t i;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count || source->has_memory_clobber ||
        source->clobber_count || source->register_clobber_count ||
        source->output_count != (form == 1U ? 1U : 0U) ||
        source->input_count != (form == 1U ? 0U : 3U) ||
        (form == 1U ? !source->outputs : !source->inputs)) return false;
    if (form == 1U) {
        if (!target_constraint_is(&source->outputs[0], "+a")) return false;
        expr = minic_c0_program_expression(
            program, source->outputs[0].expression);
        if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expr->type, minic_type_unsigned_long()))
            return false;
        contract->operand_count = 1U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
        return true;
    }
    for (i = 0U; i < 3U; ++i) {
        if (!target_constraint_is(&source->inputs[i], tpause_constraints[i]))
            return false;
        expr = minic_c0_program_expression(
            program, source->inputs[i].expression);
        if (!expr || !minic_type_equal(expr->type,
                                       minic_type_unsigned_int())) return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 3U;
    return true;
}

/* The identity-mapping bootstrap intentionally dereferences the physical
 * address and consumes the result in R9 while declaring R9 clobbered.  GNU
 * 'g' must not be modeled as an address-only input or as a no-op barrier. */
bool minic_x86_64_linux_identity_load_form(const char *text, size_t length) {
    return x86_linux_compact_asm_equals(
               text, length, "mov%[address],%%r9") ||
           x86_linux_compact_asm_equals(text, length, "mov%0,%%r9");
}

static bool x86_linux_identity_load_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_identity_load_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count || source->input_count != 1U ||
        !source->inputs || !target_constraint_is(&source->inputs[0], "g") ||
        source->clobber_count != 2U || !source->has_memory_clobber ||
        source->register_clobber_count != 1U ||
        !clobber_is(&source->register_clobbers[0], "r9")) return false;
    expr = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!expr || expr->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(expr->type, minic_type_unsigned_long())) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

bool minic_x86_64_linux_ipv6_checksum_form(const char *text, size_t length) {
    return x86_linux_compact_asm_equals(text, length,
             "addq(%[saddr]),%[sum]adcq8(%[saddr]),%[sum]"
             "adcq(%[daddr]),%[sum]adcq8(%[daddr]),%[sum]"
             "adcq$0,%[sum]") ||
           x86_linux_compact_asm_equals(text, length,
             "addq(%2),%0adcq8(%2),%0adcq(%3),%0"
             "adcq8(%3),%0adcq$0,%0");
}

static bool x86_linux_ipv6_checksum_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const constraints[] = {"=r", "[sum]", "r", "r"};
    const MinicExpression *expr;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_ipv6_checksum_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count || source->output_count != 1U ||
        source->input_count != 3U || !source->outputs || !source->inputs)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *operand = i == 0U
            ? &source->outputs[0] : &source->inputs[i - 1U];
        if (!target_constraint_is(operand, constraints[i])) return false;
        expr = minic_c0_program_expression(program, operand->expression);
        if (!expr || (i >= 2U ? !minic_type_is_pointer(expr->type)
                             : !minic_type_is_integer(expr->type)) ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type)))) return false;
        contract->kinds[i] = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux csum_partial has three related CF-preserving x86-64 operations:
 * register+register, five independent qword memory operands, and an explicit
 * base pointer with a GNU "m" alias region of 8/16/32 bytes.  Each complete
 * template is matched; a generic ADD/ADC prefix is not sufficient because
 * the final adcq $0 must consume the preceding carry bit. */
unsigned int minic_x86_64_linux_csum_partial_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "addq%1,%0adcq$0,%0",
        "addq%1,%0adcq%2,%0adcq%3,%0adcq%4,%0adcq%5,%0adcq$0,%0",
        "addq0*8(%[src]),%[res]adcq1*8(%[src]),%[res]"
            "adcq2*8(%[src]),%[res]adcq3*8(%[src]),%[res]adcq$0,%[res]",
        "addq0*8(%[src]),%[res]adcq1*8(%[src]),%[res]adcq$0,%[res]",
        "addq0*8(%[src]),%[res]adcq$0,%[res]",
        "addq%[trail],%[res]adcq$0,%[res]",
        /* Core safely canonicalizes named GNU operands into positionals;
         * emit-time ISA matching must recognize the same exact instruction
         * stream without permitting additional templates at source. */
        "addq0*8(%1),%0adcq1*8(%1),%0adcq2*8(%1),%0"
            "adcq3*8(%1),%0adcq$0,%0",
        "addq0*8(%1),%0adcq1*8(%1),%0adcq$0,%0",
        "addq0*8(%1),%0adcq$0,%0"
    };
    size_t i;
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        if (x86_linux_compact_asm_equals(text, length, forms[i]))
            return (unsigned int)(i < 6U ? i + 1U : i - 3U);
    }
    return 0U;
}

static bool x86_linux_csum_partial_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_csum_partial_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *expression;
    const MinicArrayType *array;
    MinicType type;
    size_t i;
    const size_t count = form == 1U || form == 6U ? 2U : form == 2U ? 6U : 3U;
    if (program == NULL || source == NULL || contract == NULL || form == 0U ||
        source->is_volatile || source->is_goto || source->output_count != 1U ||
        source->input_count != count - 1U || !source->outputs || !source->inputs ||
        source->label_count || source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "+r")) return false;
    expression = minic_c0_program_expression(program, source->outputs[0].expression);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(expression->type) ||
        !minic_type_unqualified(expression->type, &type) ||
        !minic_type_is_integer(type) ||
        (type.integer_rank != MINIC_INTEGER_RANK_LONG &&
         type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)) return false;
    contract->operand_count = count;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    for (i = 1U; i < count; ++i) {
        const MinicInlineAsmOperand *operand = &source->inputs[i - 1U];
        const char *constraint = form == 1U || form == 6U ? "r" :
            form == 2U ? "m" : i == 1U ? "r" : "m";
        if (!target_constraint_is(operand, constraint)) return false;
        expression = minic_c0_program_expression(program, operand->expression);
        if (expression == NULL || !minic_type_unqualified(expression->type, &type))
            return false;
        if (form == 1U || form == 6U || (form >= 3U && i == 1U)) {
            if ((form == 1U || form == 6U) ? !minic_type_is_integer(type) :
                !minic_type_is_pointer(type)) return false;
            contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        } else {
            if (form == 2U) {
                if (expression->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_is_integer(type) ||
                    (type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                     type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG)) return false;
            } else {
                array = minic_type_is_array(type) ?
                    minic_c0_program_array_type(program, type.array_type_id) : NULL;
                if (array != NULL) {
                    MinicType element;
                    if (expression->value_category != MINIC_VALUE_LVALUE ||
                        array->element_count !=
                            (form == 3U ? 32U : form == 4U ? 16U : 8U) ||
                        !minic_type_unqualified(array->element_type, &element) ||
                        !minic_type_is_char_integer(element)) return false;
                } else {
                    MinicType element;
                    /* Array-qualified GNU m operands may be stored in the
                     * AST as ADDRESS_OF of the lvalue array's first char.
                     * Core lower_address owns that canonical conversion. */
                    if (expression->kind != MINIC_EXPRESSION_ADDRESS_OF ||
                        !minic_type_is_pointer(type) ||
                        !minic_type_pointee(type, &element) ||
                        !minic_type_unqualified(element, &element) ||
                        !minic_type_is_char_integer(element)) return false;
                }
            }
            /* GNU char-array `m` operands are frontend-canonicalized into
             * ADDRESS_OF RVALUE pointers.  They name an alias region, not a
             * C scalar load; transport the address and mark an implicit
             * memory effect.  Actual qword reads use GNU's named `src`. */
            contract->kinds[i] = form == 2U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        }
    }
    contract->implicit_memory_effect = form != 1U && form != 6U;
    return true;
}

static bool x86_linux_delay_mul_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const output_constraints[] = {"=d", "=&a"};
    static const char *const input_constraints[] = {"1", "0"};
    const MinicExpression *expr;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_delay_mul_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count || source->output_count != 2U ||
        source->input_count != 2U || !source->outputs || !source->inputs)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicInlineAsmOperand *op = i < 2U
            ? &source->outputs[i] : &source->inputs[i - 2U];
        if (!target_constraint_is(op, i < 2U
            ? output_constraints[i] : input_constraints[i - 2U])) return false;
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || !minic_type_is_integer(expr->type) ||
            (i < 2U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))) ||
            !minic_type_equal(expr->type, i == 1U
                ? minic_type_int() : minic_type_unsigned_long()))
            return false;
        contract->kinds[i] = i < 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = 4U;
    return true;
}

static bool x86_linux_boot_flags_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_boot_flags_form(
        source->template_text, source->template_length) : 0U;
    static const char *const cpuid_outputs[] = {"=a", "=b", "=c", "=d"};
    size_t i, count;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count || source->label_count) return false;
    if (form == 1U || form == 2U) {
        const MinicExpression *memory, *index;
        if (source->output_count != 1U || !source->outputs ||
            source->input_count != (form == 1U ? 2U : 1U) || !source->inputs ||
            !target_constraint_is(&source->outputs[0],
                form == 1U ? "=@ccc" : "+m") ||
            (form == 1U && !target_constraint_is(&source->inputs[0], "m")) ||
            !target_constraint_is(&source->inputs[form == 1U ? 1U : 0U], "Ir"))
            return false;
        memory = minic_c0_program_expression(program,
            form == 1U ? source->inputs[0].expression :
                         source->outputs[0].expression);
        index = minic_c0_program_expression(program,
            source->inputs[form == 1U ? 1U : 0U].expression);
        if (!memory || memory->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_is_integer(memory->type) ||
            !index || !minic_type_is_integer(index->type)) return false;
        if (form == 1U) {
            const MinicExpression *flag = minic_c0_program_expression(
                program, source->outputs[0].expression);
            if (!flag || flag->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_equal(flag->type, minic_type_bool())) return false;
        }
        contract->operand_count = form == 1U ? 3U : 2U;
        contract->kinds[0] = form == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1] = form == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (form == 1U)
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->implicit_memory_effect = true;
        return true;
    }
    if (form == 3U) {
        if (!source->is_volatile || source->output_count != 4U ||
            source->input_count != 2U || !source->outputs || !source->inputs ||
            !target_constraint_is(&source->inputs[0], "0") ||
            !target_constraint_is(&source->inputs[1], "2")) return false;
        for (i = 0U; i < 4U; ++i)
            if (!target_constraint_is(&source->outputs[i],cpuid_outputs[i]))
                return false;
        count = 6U;
    } else {
        if (!source->is_volatile || source->output_count != 2U ||
            source->input_count != 1U || !source->outputs || !source->inputs ||
            !target_constraint_is(&source->outputs[0], "=&r") ||
            !target_constraint_is(&source->outputs[1], "=&r") ||
            !target_constraint_is(&source->inputs[0], "ri")) return false;
        count = 3U;
    }
    for (i = 0U; i < count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *operand = output
            ? &source->outputs[i] : &source->inputs[i-source->output_count];
        const MinicExpression *e = minic_c0_program_expression(
            program, operand->expression);
        if (!e || !minic_type_is_integer(e->type) ||
            (output && (e->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(e->type)))) return false;
        contract->kinds[i] = output
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = count;
    contract->implicit_memory_effect = form == 4U;
    return true;
}

/* Target-only descriptor for all 8 GNU Linux atomic RMWcc forms.
 * This is the only source/machine ALU and flag-sign table. */
static const struct {
    MinicX86AtomicCCDesc desc;
    const char *flag;
} x86_atomic_cc_forms[] = {
    {{"decl", 4U, false, false}, "e"},
    {{"incl", 4U, false, false}, "e"},
    {{"decq", 8U, false, false}, "e"},
    {{"incq", 8U, false, false}, "e"},
    {{"subl", 4U, true, false}, "e"},
    {{"addl", 4U, true, true}, "s"},
    {{"subq", 8U, true, false}, "e"},
    {{"addq", 8U, true, true}, "s"}
};
const MinicX86AtomicCCDesc *minic_x86_64_atomic_cc_description(
    MinicX86AtomicCC form) {
    const size_t id = (size_t)form;
    return id >= MINIC_X86_ATOMIC_CC_DEC32 &&
           id <= MINIC_X86_ATOMIC_CC_ADD64_SIGN ?
        &x86_atomic_cc_forms[id - 1U].desc : NULL;
}
/* Exact original GNU SMP patchtable + LOCK + operand + CC_SET text,
 * whether source-named or numeric after Common inline ASM normalization.
 * Never forward source ASM or any unrecognized suffix to GAS. */
MinicX86AtomicCC minic_x86_64_atomic_cc_form(const char *text, size_t len) {
    static const char prefix[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256], expected[256];
    size_t i, n = 0U, id, variant, lock_variant;
    if (!text || len == 0U || len >= sizeof(compact))
        return MINIC_X86_ATOMIC_CC_NONE;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_ATOMIC_CC_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (id = 0U; id < sizeof(x86_atomic_cc_forms) /
                       sizeof(x86_atomic_cc_forms[0]); ++id) {
        const MinicX86AtomicCCDesc *d = &x86_atomic_cc_forms[id].desc;
        for (variant = 0U; variant < 2U; ++variant)
          for (lock_variant = 0U; lock_variant < 2U; ++lock_variant) {
            const char *mem = variant ? "%0" : "%[var]";
            const char *value = variant ? "%2" : "%[val]";
            const char *prefix_text = lock_variant ? prefix : "";
            const int written = d->has_value ?
                snprintf(expected, sizeof(expected),
                    "%s%s%s,%s/*outputconditioncode%s*/",
                    prefix_text, d->opcode, value, mem,
                    x86_atomic_cc_forms[id].flag) :
                snprintf(expected, sizeof(expected),
                    "%s%s%s/*outputconditioncode%s*/",
                    prefix_text, d->opcode, mem,
                    x86_atomic_cc_forms[id].flag);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (MinicX86AtomicCC)(id + 1U);
        }
    }
    return MINIC_X86_ATOMIC_CC_NONE;
}

bool minic_x86_64_atomic_cc_locked_form(const char *text, size_t len) {
    static const char marker[] = ".smp_locks";
    size_t i;
    if (!text || minic_x86_64_atomic_cc_form(text, len) ==
                     MINIC_X86_ATOMIC_CC_NONE) return false;
    for (i = 0U; i + sizeof(marker) - 1U <= len; ++i)
        if (memcmp(text + i, marker, sizeof(marker) - 1U) == 0)
            return true;
    return false;
}

/* Queued spinlock bit-test and unlock-byte flag probes preserve both
 * SMP patch records and x86 carry/sign flag outputs. */
unsigned int minic_x86_64_linux_lock_bitcc_form(
    const char *text, size_t len) {
    static const char prefix[] =
        ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;";
    char compact[256];
    size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (text[i] == ' ' || text[i] == '\t' ||
            text[i] == '\n' || text[i] == '\r') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = text[i];
    }
    compact[n] = '\0';
    if (strncmp(compact, prefix, sizeof(prefix)-1U) != 0) return 0U;
    if (strcmp(compact + sizeof(prefix)-1U,
        "btsl%[val],%[var]/*outputconditioncodec*/") == 0 ||
        strcmp(compact + sizeof(prefix)-1U,
        "btsl%2,%0/*outputconditioncodec*/") == 0) return 1U;
    if (strcmp(compact + sizeof(prefix)-1U,
        "andb%2,%1/*outputconditioncodes*/") == 0) return 2U;
    return 0U;
}

static bool x86_linux_lock_bitcc_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_lock_bitcc_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *mem, *flag, *input;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count || source->output_count != 2U ||
        source->input_count != 1U || !source->outputs || !source->inputs ||
        !source->has_memory_clobber || source->clobber_count != 1U ||
        source->register_clobber_count != 0U ||
        !target_constraint_is(&source->outputs[form == 1U ? 0U : 1U], "+m") ||
        !target_constraint_is(&source->outputs[form == 1U ? 1U : 0U],
                              form == 1U ? "=@ccc" : "=@ccs") ||
        !target_constraint_is(&source->inputs[0],
                              form == 1U ? "I" : "ir")) return false;
    mem = minic_c0_program_expression(program,
        source->outputs[form == 1U ? 0U : 1U].expression);
    flag = minic_c0_program_expression(program,
        source->outputs[form == 1U ? 1U : 0U].expression);
    input = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!mem || !flag || !input ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_integer(mem->type) ||
        !minic_type_equal(flag->type, minic_type_bool()) ||
        !minic_type_is_integer(input->type)) return false;
    contract->operand_count = 3U;
    contract->kinds[0] = form == 1U
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
        : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = form == 1U
        ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
        : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* REPE CMPSB exposes ZF and modified pointers/count, DIVL consumes implicit
 * EDX:EAX, REP MOVSQ/MOVSB exports all three fixed-register outputs. */
unsigned int minic_x86_64_linux_boot_string_form(
    const char *text, size_t length) {
    if (x86_linux_compact_asm_equals(text, length,
        "repe;cmpsb/*outputconditioncodenz*/")) return 1U;
    if (x86_linux_compact_asm_equals(text, length, "divl%2")) return 2U;
    if (x86_linux_compact_asm_equals(text, length,
        "rep;movsqmovq%4,%%rcxrep;movsb")) return 3U;
    return 0U;
}

static bool x86_linux_boot_string_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const cmps_out[] = {
        "=@ccnz", "+D", "+S", "+c"
    };
    static const char *const div_out[] = {"=a", "=d"};
    static const char *const div_in[] = {"rm", "0", "1"};
    static const char *const movs_out[] = {"=&c", "=&D", "=&S"};
    static const char *const movs_in[] = {"0", "g", "1", "2"};
    const unsigned int form = source ? minic_x86_64_linux_boot_string_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *expr;
    const size_t outputs = form == 1U ? 4U : form == 2U ? 2U : 3U;
    const size_t inputs = form == 1U ? 0U : form == 2U ? 3U : 4U;
    size_t i;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->label_count || source->register_clobber_count ||
        source->output_count != outputs || source->input_count != inputs ||
        !source->outputs || (inputs && !source->inputs) ||
        source->clobber_count != (form == 3U ? 1U : 0U) ||
        source->has_memory_clobber != (form == 3U) ||
        source->is_volatile != (form == 3U)) return false;
    for (i = 0U; i < outputs + inputs; ++i) {
        const bool output = i < outputs;
        const size_t j = output ? i : i-outputs;
        const MinicInlineAsmOperand *operand = output
            ? &source->outputs[j] : &source->inputs[j];
        const char *constraint = form == 1U ? cmps_out[j] :
            form == 2U ? (output ? div_out[j] : div_in[j]) :
                         (output ? movs_out[j] : movs_in[j]);
        if (!target_constraint_is(operand, constraint)) return false;
        expr = minic_c0_program_expression(program, operand->expression);
        if (!expr || (output &&
            (expr->value_category != MINIC_VALUE_LVALUE ||
             minic_type_is_const(expr->type)))) return false;
        if (form == 1U) {
            if (!(i == 0U ? minic_type_equal(expr->type, minic_type_bool()) :
                  i == 3U ? minic_type_equal(expr->type,
                                              minic_type_unsigned_long()) :
                            minic_type_is_pointer(expr->type))) return false;
        } else if (form == 2U) {
            if (!minic_type_equal(expr->type,
                                  minic_type_unsigned_int())) return false;
        } else if (output) {
            if (!minic_type_equal(expr->type, minic_type_long())) return false;
        } else if (j < 2U ?
            !minic_type_equal(expr->type, minic_type_unsigned_long()) :
            !minic_type_is_pointer(expr->type)) return false;
        contract->kinds[i] = form == 1U ?
            (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) :
            output ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = outputs + inputs;
    contract->implicit_memory_effect = form != 2U;
    return true;
}

/* vDSO fallback syscall reads three fixed Linux ABI input registers and
 * produces RAX. Clock queries expose an additional =m timespec lvalue; the
 * gettimeofday variant carries a "memory" compiler barrier instead. */
bool minic_x86_64_linux_vdso_syscall_form(
    const char *text, size_t length) {
    return text && length == sizeof("syscall") - 1U &&
           memcmp(text, "syscall", length) == 0;
}

static bool x86_linux_vdso_syscall_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const bool clock_form = source && source->output_count == 2U;
    const MinicExpression *ret, *outmem, *number, *arg1, *arg2;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_vdso_syscall_form(
            source->template_text, source->template_length) ||
        source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != (clock_form ? 2U : 1U) ||
        source->input_count != 3U || !source->outputs || !source->inputs ||
        source->clobber_count != (clock_form ? 2U : 1U) ||
        source->has_memory_clobber == clock_form ||
        source->register_clobber_count != (clock_form ? 2U : 0U) ||
        (clock_form &&
         (!clobber_is(&source->register_clobbers[0], "rcx") ||
          !clobber_is(&source->register_clobbers[1], "r11"))) ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        (clock_form && !target_constraint_is(&source->outputs[1], "=m")) ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "D") ||
        !target_constraint_is(&source->inputs[2], "S")) return false;
    ret = minic_c0_program_expression(program,source->outputs[0].expression);
    outmem = clock_form ? minic_c0_program_expression(program,
        source->outputs[1].expression) : NULL;
    number = minic_c0_program_expression(program,source->inputs[0].expression);
    arg1 = minic_c0_program_expression(program,source->inputs[1].expression);
    arg2 = minic_c0_program_expression(program,source->inputs[2].expression);
    if (!ret || ret->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(ret->type,minic_type_long()) ||
        (clock_form && (!outmem ||
            outmem->value_category != MINIC_VALUE_LVALUE)) ||
        !number || !minic_type_is_integer(number->type) ||
        !arg1 || !(clock_form ? minic_type_is_integer(arg1->type) :
                                minic_type_is_pointer(arg1->type)) ||
        !arg2 || !minic_type_is_pointer(arg2->type)) return false;
    contract->operand_count = clock_form ? 5U : 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    if (clock_form)
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[clock_form ? 2U : 1U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[clock_form ? 3U : 2U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[clock_form ? 4U : 3U] =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* vDSO getcpu switches from its original LSL selector-limit read to RDPID
 * using the Linux alternative-instruction patch table (CPUID 16*32+22).
 * Match the whole table, lengths and opcode bytes, never a leading mnemonic. */
bool minic_x86_64_linux_vdso_cpunode_form(
    const char *text, size_t length) {
    static const char *const prefix =
        "#ALT:oldnstr661:";
    static const char *const suffix =
        "662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(16*32+22)"
        ".byte663b-661b.byte6651f-6641f"
        ".popsection.pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:.byte0xf3,0x0f,0xc7,0xf8"
        "6651:.popsection";
    char *expected;
    char compact[640];
    size_t i,n=0U;
    if (!text || !length || length>=sizeof(compact)) return false;
    for (i=0U;i<length;++i) {
        if (text[i]==' ' || text[i]=='\t' ||
            text[i]=='\r' || text[i]=='\n') continue;
        if (n+1U>=sizeof(compact)) return false;
        compact[n++]=text[i];
    }
    compact[n]='\0';
    if (strncmp(compact,prefix,strlen(prefix))!=0) return false;
    expected=compact+strlen(prefix);
    if (strncmp(expected,"lsl%[seg],%[p]",strlen("lsl%[seg],%[p]"))==0)
        expected+=strlen("lsl%[seg],%[p]");
    else if (strncmp(expected,"lsl%2,%0",strlen("lsl%2,%0"))==0)
        expected+=strlen("lsl%2,%0");
    else return false;
    return strcmp(expected,suffix)==0;
}

static bool x86_linux_vdso_cpunode_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *p,*zero,*selector;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_vdso_cpunode_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count!=1U || source->input_count!=2U ||
        !source->outputs || !source->inputs ||
        source->has_memory_clobber || source->clobber_count ||
        source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0],"=a") ||
        !target_constraint_is(&source->inputs[0],"i") ||
        !target_constraint_is(&source->inputs[1],"r")) return false;
    p=minic_c0_program_expression(program,source->outputs[0].expression);
    zero=minic_c0_program_expression(program,source->inputs[0].expression);
    selector=minic_c0_program_expression(program,source->inputs[1].expression);
    if (!p || p->value_category!=MINIC_VALUE_LVALUE ||
        !minic_type_equal(p->type,minic_type_unsigned_int()) ||
        !zero || !minic_type_is_integer(zero->type) ||
        !selector || !minic_type_is_integer(selector->type)) return false;
    contract->operand_count=3U;
    contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect=true;
    return true;
}

static bool x86_linux_atomic_cc_source(
    const MinicC0Program *program, const MinicInlineAsm *src) {
    const MinicExpression *mem, *flag, *value;
    MinicType mt, ft, vt;
    const MinicX86AtomicCC form = src ?
        minic_x86_64_atomic_cc_form(src->template_text, src->template_length) :
        MINIC_X86_ATOMIC_CC_NONE;
    const MinicX86AtomicCCDesc *d =
        minic_x86_64_atomic_cc_description(form);
    if (!program || !src || !d || !src->is_volatile || src->is_goto ||
        !src->outputs || src->output_count != 2U ||
        src->input_count != (d->has_value ? 1U : 0U) ||
        (d->has_value && !src->inputs) || src->label_count ||
        src->clobber_count != 1U || !src->has_memory_clobber ||
        src->register_clobber_count ||
        !target_constraint_is(&src->outputs[0], "+m") ||
        !target_constraint_is(&src->outputs[1],
                              d->flag_sign ? "=@ccs" : "=@cce") ||
        (d->has_value && !target_constraint_is(&src->inputs[0], "er")))
        return false;
    mem = minic_c0_program_expression(program, src->outputs[0].expression);
    flag = minic_c0_program_expression(program, src->outputs[1].expression);
    if (!mem || !flag || mem->value_category != MINIC_VALUE_LVALUE ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(mem->type) || minic_type_is_const(flag->type) ||
        !minic_type_unqualified(mem->type, &mt) ||
        !minic_type_unqualified(flag->type, &ft) ||
        !minic_type_equal(ft, minic_type_bool()))
        return false;
    if (d->width == 4U ?
        !minic_type_equal(mt, minic_type_int()) :
        (!minic_type_equal(mt, minic_type_long()) &&
         !minic_type_equal(mt, minic_type_long_long())))
        return false;
    if (d->has_value) {
        value = minic_c0_program_expression(program, src->inputs[0].expression);
        if (!value || !minic_type_unqualified(value->type, &vt) ||
            (!minic_type_equal(vt, mt) &&
             !(d->width == 8U &&
               (minic_type_equal(vt, minic_type_long()) ||
                minic_type_equal(vt, minic_type_long_long())) &&
               (minic_type_equal(mt, minic_type_long()) ||
                minic_type_equal(mt, minic_type_long_long()))))) return false;
    }
    return true;
}
/* Original GNU Linux 6.6.143 x86 bitops: exact source templates, not
 * a generic inline assembly passthrough. The same table is the ONLY
 * authoritative ISA/memory/flag descriptor used by source and machine.
 * Names become canonical numeric operands in shared Core. */
static const struct {
    const char *literal;
    const char *canonical;
    MinicX86BitopsDesc desc;
} x86_linux_bitops[] = {
    {"btq%2,%1/*outputconditioncodec*/",
     "btq%2,%1/*outputconditioncodec*/",
     {"btq",0,1,0,0}},
    {"btsq%2,%1/*outputconditioncodec*/",
     "btsq%2,%1/*outputconditioncodec*/",
     {"btsq",0,1,0,1}},
    {"btrq%2,%1/*outputconditioncodec*/",
     "btrq%2,%1/*outputconditioncodec*/",
     {"btrq",0,1,0,1}},
    {"btcq%2,%1/*outputconditioncodec*/",
     "btcq%2,%1/*outputconditioncodec*/",
     {"btcq",0,1,0,1}},
    {"btsq%1,%0","btsq%1,%0",{"btsq",0,0,0,1}},
    {"btrq%1,%0","btrq%1,%0",{"btrq",0,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%1,%0",
     {"btsq",1,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%1,%0",
     {"btrq",1,0,0,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%[val],%[var]/*outputconditioncodec*/",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btsq%2,%0/*outputconditioncodec*/",
     {"btsq",1,1,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%[val],%[var]/*outputconditioncodec*/",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;btrq%2,%0/*outputconditioncodec*/",
     {"btrq",1,1,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;orb%b1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;orb%b1,%0",
     {"orb",1,0,1,1}},
    {".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;andb%b1,%0",
     ".pushsection.smp_locks,\"a\".balign4.long671f-..popsection671:lock;andb%b1,%0",
     {"andb",1,0,1,1}}
};
MinicX86BitopsForm minic_x86_64_linux_bitops_form(
    const char *text,size_t length) {
    char compact[256];
    size_t i,j=0U;
    if (!text||!length||length>=sizeof(compact))
        return MINIC_X86_BITOPS_NONE;
    for(i=0U;i<length;++i) {
        const char ch=text[i];
        if (ch==' '||ch=='\t'||ch=='\n'||ch=='\r')continue;
        if(j+1U>=sizeof(compact))return MINIC_X86_BITOPS_NONE;
        compact[j++]=ch;
    }
    compact[j]='\0';
    for(i=0U;i<sizeof(x86_linux_bitops)/sizeof(x86_linux_bitops[0]);++i)
        if(strcmp(compact,x86_linux_bitops[i].literal)==0||
           strcmp(compact,x86_linux_bitops[i].canonical)==0)
            return (MinicX86BitopsForm)(i+1U);
    return MINIC_X86_BITOPS_NONE;
}
const MinicX86BitopsDesc *minic_x86_64_bitops_description(
    MinicX86BitopsForm form) {
    const size_t index = (size_t)form;
    if(index==0U||index>sizeof(x86_linux_bitops)/sizeof(x86_linux_bitops[0]))
        return NULL;
    return &x86_linux_bitops[index-1U].desc;
}
static bool x86_linux_bitops_source(
    const MinicC0Program *program,const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicX86BitopsForm form=src ?
        minic_x86_64_linux_bitops_form(
            src->template_text,src->template_length):MINIC_X86_BITOPS_NONE;
    const MinicX86BitopsDesc *desc=minic_x86_64_bitops_description(form);
    const bool byte_rmw = form==MINIC_X86_BITOPS_LOCK_OR_BYTE ||
                          form==MINIC_X86_BITOPS_LOCK_AND_BYTE;
    const bool needs_memory_clobber = !byte_rmw ||
                                      form==MINIC_X86_BITOPS_LOCK_OR_BYTE;
    const MinicInlineAsmOperand *mem,*bit,*flag;
    const MinicExpression *m,*b,*f;
    MinicType mem_type,bit_type,flag_type;
    if(!program||!src||!contract||!desc||src->is_goto||
       src->label_count||src->register_clobber_count||
       src->has_memory_clobber!=needs_memory_clobber||
       src->clobber_count!=(needs_memory_clobber?1U:0U)||
       src->output_count!=(desc->has_flag?(desc->memory_output?2U:1U):
                           (desc->memory_output?1U:0U))||
       src->input_count!=(desc->memory_output?1U:2U)||
       (src->output_count&&!src->outputs)||!src->inputs||
       (desc->is_locked&&!src->is_volatile))
        return false;
    flag=desc->has_flag?(desc->memory_output?&src->outputs[1]:
                                         &src->outputs[0]):NULL;
    mem=desc->memory_output?&src->outputs[0]:&src->inputs[0];
    bit=desc->memory_output?&src->inputs[0]:&src->inputs[1];
    if(!target_constraint_is(mem,desc->memory_output?"+m":"m")||
       !target_constraint_is(bit,byte_rmw?"iq":"Ir")||
       (flag&&!target_constraint_is(flag,"=@ccc")))
        return false;
    m=minic_c0_program_expression(program,mem->expression);
    b=minic_c0_program_expression(program,bit->expression);
    f=flag?minic_c0_program_expression(program,flag->expression):NULL;
    if(!m||!b||m->value_category!=MINIC_VALUE_LVALUE||
       (desc->writes_memory&&minic_type_is_const(m->type))||
       !minic_type_unqualified(m->type,&mem_type)||
       !minic_type_unqualified(b->type,&bit_type)||
       (byte_rmw ?
         /* GNU Linux WBYTE_ADDR may be volatile char, signed char, or
          * unsigned char. Char *RANK*, rather than three exact type
          * constructors, is the true 8-bit C-memory width contract:
          * the original parsed type can carry extra source qualifiers.
          * Do not broaden to _Bool or enum or int, or widen to 64-bit. */
         (!minic_type_is_integer(mem_type) ||
          mem_type.pointer_depth != 0U ||
          mem_type.integer_rank != MINIC_INTEGER_RANK_CHAR) :
         (!minic_type_equal(mem_type,minic_type_long())&&
          !minic_type_equal(mem_type,minic_type_unsigned_long())))||
       (byte_rmw?
         (!minic_type_equal(bit_type,minic_type_int())&&
          !minic_type_equal(bit_type,minic_type_unsigned_int())):
         (!minic_type_equal(bit_type,minic_type_long())&&
          !minic_type_equal(bit_type,minic_type_unsigned_long())))||
       (flag&&(!f||f->value_category!=MINIC_VALUE_LVALUE||
          minic_type_is_const(f->type)||
          !minic_type_unqualified(f->type,&flag_type)||
          !minic_type_equal(flag_type,minic_type_bool()))))
        return false;
    contract->operand_count=src->output_count+src->input_count;
    if(desc->memory_output) {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        if(desc->has_flag) {
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        } else {
            contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        }
    } else if(desc->has_flag) {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1]=desc->writes_memory ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        contract->kinds[2]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    } else {
        contract->kinds[0]=MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1]=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    return true;
}
/* Exact x86 Linux word-sized GNU BSF/BSR spelling. The REP prefix on
 * Linux __ffs is deliberate: it becomes TZCNT where available, and
 * preserves BSF bit-index semantics for nonzero inputs on older CPUs.
 * An all-zero input has no defined output; callers must not depend on it. */
/* Decode complete GNU source/machine bit-scan templates once.
 * Tied input #2 is materialized explicitly as the pre-BSR destination,
 * so a zero source is not silently changed to an uninitialized register. */
MinicX86BitScanForm minic_x86_64_asm_bit_scan_form(
    const char *text, size_t length) {
    static const struct { const char *text; MinicX86BitScanForm form; } forms[] = {
        {"rep;bsf%1,%0", MINIC_X86_BIT_SCAN_BSF64},
        {"bsr%1,%0", MINIC_X86_BIT_SCAN_BSR64},
        {"bsfl%1,%0", MINIC_X86_BIT_SCAN_BSF32_TIED},
        {"bsrl%1,%0", MINIC_X86_BIT_SCAN_BSR32_TIED},
        {"bsrq%1,%q0", MINIC_X86_BIT_SCAN_BSR64_READWRITE}
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact))
        return MINIC_X86_BIT_SCAN_NONE;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (n + 1U >= sizeof(compact)) return MINIC_X86_BIT_SCAN_NONE;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].text) == 0)
            return forms[i].form;
    return MINIC_X86_BIT_SCAN_NONE;
}
bool minic_x86_64_asm_bit_scan_template(
    const char *text, size_t length, bool *is_reverse) {
    const MinicX86BitScanForm form = minic_x86_64_asm_bit_scan_form(text, length);
    if (!is_reverse ||
        (form != MINIC_X86_BIT_SCAN_BSF64 && form != MINIC_X86_BIT_SCAN_BSR64))
        return false;
    *is_reverse = form == MINIC_X86_BIT_SCAN_BSR64;
    return true;
}

/* The GNU =r + matched-digit family owns only explicit register operands.
 * No hidden target machine registers, jumps, labels, function calls or
 * directives. Every template instruction is made of a conservative x86
 * scalar carry/move mnemonic and references only %0..%{outputs-1}. */
/* An explicitly bounded grammar for the BusyBox/TLS P-256 register +
 * pointer-memory inline ASM. No source bytes may reach GNU AS except:
 *   %N, (%N), K*8(%N) for K=1..3, $0 and $1.
 * Core/machine independently verify operand roles and physical registers.
 * We retain the already-supported pointer forms; rejecting them as
 * register-only would cause a regression, and ignoring unchecked suffixes
 * permitted an injected second instruction. */
static bool x86_matched_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}
static bool x86_matched_ref(const char *text, size_t len, size_t *cursor,
                            size_t outputs, size_t *references) {
    size_t i = *cursor;
    if (i >= len || text[i] != '%' || i + 1U >= len ||
        text[i + 1U] < '0' ||
        (size_t)(text[i + 1U] - '0') >= outputs)
        return false;
    *cursor = i + 2U;
    ++*references;
    return true;
}
static bool x86_matched_operand(const char *text, size_t len, size_t *cursor,
                                size_t outputs, size_t *refs) {
    size_t i = *cursor;
    if (i >= len) return false;
    if (text[i] == '$') {
        ++i;
        if (i >= len || (text[i] != '0' && text[i] != '1'))
            return false;
        ++i;
    } else {
        bool memory = false;
        if (i + 4U < len &&
            text[i] >= '1' && text[i] <= '3' &&
            text[i + 1U] == '*' && text[i + 2U] == '8' &&
            text[i + 3U] == '(') {
            i += 4U;
            memory = true;
        } else if (text[i] == '(') {
            ++i;
            memory = true;
        }
        if (!x86_matched_ref(text, len, &i, outputs, refs))
            return false;
        if (memory) {
            if (i >= len || text[i] != ')')
                return false;
            ++i;
        }
    }
    *cursor = i;
    return true;
}
bool minic_x86_64_asm_matched_template(const char *text,
                                        size_t length, size_t outputs) {
    static const char *const binary_ops[] = {
        "movq", "addq", "adcq", "subq", "sbbq"
    };
    size_t i = 0U, lines = 0U, refs = 0U, k;
    if (text == NULL || length == 0U || length > 1024U ||
        outputs == 0U || outputs > 4U)
        return false;
    while (i < length) {
        bool matched = false;
        while (i < length &&
               (x86_matched_space(text[i]) || text[i] == '\n' ||
                text[i] == ';'))
            ++i;
        if (i == length) break;
        if (i + 3U <= length && memcmp(text + i, "cmc", 3U) == 0) {
            i += 3U;
        } else {
            for (k = 0U; k < sizeof(binary_ops) / sizeof(binary_ops[0]); ++k) {
                if (i + 4U <= length &&
                    memcmp(text + i, binary_ops[k], 4U) == 0) {
                    matched = true;
                    i += 4U;
                    break;
                }
            }
            if (!matched || i >= length || !x86_matched_space(text[i]))
                return false;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (!x86_matched_operand(text, length, &i, outputs, &refs))
                return false;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (i == length || text[i] != ',') return false;
            ++i;
            while (i < length && x86_matched_space(text[i])) ++i;
            if (!x86_matched_operand(text, length, &i, outputs, &refs))
                return false;
        }
        while (i < length && x86_matched_space(text[i])) ++i;
        if (i < length && text[i] != '\n' && text[i] != ';')
            return false;
        ++lines;
        if (i < length) ++i;
    }
    return lines != 0U && refs != 0U;
}

/* Qualifiers are stripped once by caller. This is target-owned C
 * operand/ISA width matching, not a second generic Core type system. */
static bool x86_bit_scan_operand_type(
    MinicX86BitScanForm form, size_t operand_index, MinicType type) {
    if (form == MINIC_X86_BIT_SCAN_BSF64 ||
        form == MINIC_X86_BIT_SCAN_BSR64)
        return minic_type_equal(type, minic_type_unsigned_long());
    if (operand_index == 0U || operand_index == 2U)
        return minic_type_equal(type, minic_type_int());
    if (form == MINIC_X86_BIT_SCAN_BSR64_READWRITE)
        return minic_type_equal(type, minic_type_unsigned_long()) ||
               minic_type_equal(type, minic_type_unsigned_long_long());
    return minic_type_equal(type, minic_type_int()) ||
           minic_type_equal(type, minic_type_unsigned_int());
}

static bool qualified(const MinicC0Program *program,
                      const MinicInlineAsm *src, bool *is_mulacc, bool *is_muldiv) {
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const char *const mul_in[5] = {"0","1","2","a","m"};
    size_t i, n;
    bool generic = false;
    bool bit_scan = false;
    MinicX86BitScanForm bit_form = MINIC_X86_BIT_SCAN_NONE;
    MinicType normalized;
    if (!program || !src || !src->template_text || !src->outputs ||
        !src->inputs || src->is_goto || src->label_count != 0U ||
        !is_mulacc || !is_muldiv) return false;
    *is_mulacc = false;
    *is_muldiv = false;
    /* Original Linux native_cpuid has six operands: four register outputs
     * and only two tied inputs (EAX/ECX), plus a memory compiler barrier.
     * Preserve the legacy eight-operand CPUID contract in the same target
     * implementation. RBX save/restore is always owned by x86 machine code. */
    if (src->output_count == 4U &&
        (src->input_count == 4U || src->input_count == 2U) &&
        src->register_clobber_count == 0U &&
        src->template_length == 5U &&
        memcmp(src->template_text, "cpuid", 5U) == 0 &&
        (src->input_count == 4U ?
         (!src->has_memory_clobber && src->clobber_count == 0U) :
         (src->is_volatile && src->has_memory_clobber &&
          src->clobber_count == 1U))) {
        *is_mulacc = false;
        n = 4U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], cpuid_out[i])) return false;
        for (i = 0U; i < src->input_count; ++i)
            if (!c_is(&src->inputs[i],
                      cpuid_in[src->input_count == 2U && i == 1U ? 2U : i]))
                return false;
    } else if (src->is_volatile && src->output_count == 3U &&
               src->input_count == 5U && src->clobber_count == 2U &&
               src->register_clobber_count == 2U &&
               !src->has_memory_clobber && src->register_clobbers &&
               ((clobber_is(&src->register_clobbers[0], "cc") &&
                 clobber_is(&src->register_clobbers[1], "dx")) ||
                (clobber_is(&src->register_clobbers[0], "dx") &&
                 clobber_is(&src->register_clobbers[1], "cc"))) &&
               minic_x86_64_asm_mulacc_template(src->template_text, src->template_length)) {
        *is_mulacc = true;
        n = 3U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=rm") ||
                !c_is(&src->inputs[i], mul_in[i])) return false;
        if (!c_is(&src->inputs[3], "a") ||
            !c_is(&src->inputs[4], "m")) return false;
    } else if (src->output_count == 1U && src->input_count == 3U &&
               src->clobber_count == 1U && src->register_clobber_count == 1U &&
               !src->has_memory_clobber && src->register_clobbers &&
               clobber_is(&src->register_clobbers[0], "rdx") &&
               minic_x86_64_asm_muldiv_template(src->template_text, src->template_length) &&
               c_is(&src->outputs[0], "=a") &&
               c_is(&src->inputs[0], "a") &&
               c_is(&src->inputs[1], "rm") &&
               c_is(&src->inputs[2], "rm")) {
        *is_muldiv = true;
    } else if (src->output_count == 1U &&
               !src->is_goto && src->label_count == 0U &&
               !src->has_memory_clobber &&
               src->clobber_count == 0U && src->register_clobber_count == 0U &&
               (bit_form = minic_x86_64_asm_bit_scan_form(
                   src->template_text, src->template_length)) !=
                   MINIC_X86_BIT_SCAN_NONE &&
               (c_is(&src->inputs[0], "r") || c_is(&src->inputs[0], "rm")) &&
               (((bit_form == MINIC_X86_BIT_SCAN_BSF64 ||
                  bit_form == MINIC_X86_BIT_SCAN_BSR64) &&
                 src->input_count == 1U &&
                 c_is(&src->outputs[0], "=r")) ||
                ((bit_form == MINIC_X86_BIT_SCAN_BSF32_TIED ||
                  bit_form == MINIC_X86_BIT_SCAN_BSR32_TIED) &&
                 src->input_count == 2U &&
                 c_is(&src->outputs[0], "=r") &&
                 c_is(&src->inputs[1], "0")) ||
                (bit_form == MINIC_X86_BIT_SCAN_BSR64_READWRITE &&
                 src->input_count == 1U &&
                 c_is(&src->outputs[0], "+r")))) {
        bit_scan = true;
    } else if (src->is_volatile && src->has_memory_clobber &&
               src->clobber_count == 1U &&
               src->register_clobber_count == 0U &&
               ((src->output_count == 4U && src->input_count == 3U) ||
                (src->output_count == 3U && src->input_count == 2U) ||
                (src->output_count == 1U && src->input_count == 1U)) &&
               minic_x86_64_asm_matched_template(src->template_text, src->template_length, src->output_count)) {
        *is_mulacc = false;
        generic = true;
        n = src->output_count;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=r")) return false;
        for (i = 0U; i < src->input_count; ++i) {
            const MinicInlineAsmOperand *op = &src->inputs[i];
            if (!op->constraint_text || op->constraint_length != 1U ||
                op->constraint_text[0] != (char)('0' + i))
                return false;
        }
    } else return false;

    for (i = 0U; i < src->output_count + src->input_count; ++i) {
        const bool is_output = i < src->output_count;
        const MinicInlineAsmOperand *op = is_output
            ? &src->outputs[i] : &src->inputs[i-src->output_count];
        const MinicExpression *expr =
            minic_c0_program_expression(program, op->expression);
        const MinicType target_type =
            (*is_mulacc || bit_scan)
                ? minic_type_unsigned_long() : minic_type_unsigned_int();
        if (!expr || !minic_type_unqualified(expr->type, &normalized) ||
            (bit_scan ? !x86_bit_scan_operand_type(bit_form, i, normalized)
                      : (*is_muldiv
                           ? (!minic_type_equal(normalized, minic_type_unsigned_long()) &&
                              !minic_type_equal(normalized, minic_type_unsigned_long_long()))
                           : generic
                               ? (!minic_type_is_integer(normalized) &&
                                  !minic_type_is_pointer(normalized))
                               : !minic_type_equal(normalized, target_type))) ||
            (is_output && (expr->value_category != MINIC_VALUE_LVALUE ||
                           minic_type_is_const(expr->type))) ||
            (*is_mulacc && i == 7U &&
             expr->value_category != MINIC_VALUE_LVALUE))
            return false;
    }
    return true;
}

/* Linux 6.6 x86/include/asm/mwait.h deliberately spells MONITOR, MONITORX,
 * MWAIT and MWAITX as raw opcode bytes to compile with older assemblers.
 * Parse the exact instruction sequence and fixed A/C/D or A/B/C operand
 * assignments. The STI+MWAIT form enables interrupts for the sleeping
 * instruction; silently treating STI as an ordinary compiler fence would
 * race the idle wakeup path. The return value is a target-owned ISA ID. */
unsigned int minic_x86_64_linux_monitor_wait_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        ".byte0x0f,0x01,0xc8;",       /* MONITOR */
        ".byte0x0f,0x01,0xfa;",       /* MONITORX */
        ".byte0x0f,0x01,0xc9;",       /* MWAIT */
        ".byte0x0f,0x01,0xfb;",       /* MWAITX */
        "sti;.byte0x0f,0x01,0xc9;"    /* STI; MWAIT */
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_monitor_wait_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const regs_acd[] = {"a", "c", "d"};
    static const char *const regs_abc[] = {"a", "b", "c"};
    const unsigned int form = source ? minic_x86_64_linux_monitor_wait_form(
        source->template_text, source->template_length) : 0U;
    const size_t count = form == 1U || form == 2U || form == 4U ? 3U : 2U;
    const char *const *regs = form == 4U ? regs_abc : regs_acd;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->output_count ||
        source->input_count != count || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicInlineAsmOperand *op = &source->inputs[i];
        const MinicExpression *value = minic_c0_program_expression(
            program, op->expression);
        MinicType plain;
        if (!c_is(op, regs[i]) || !value ||
            !minic_type_unqualified(value->type, &plain) ||
            (!minic_type_is_pointer(plain) &&
             !minic_type_is_integer(plain)))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = count;
    return true;
}

/* copy_thread() and ptrace() snapshot segment selectors from live CPU state.
 * DS/ES/FS/GS are 16-bit architectural registers, even in long mode. A C memory
 * load is not equivalent, and the ordinary fixed-register allocator cannot
 * manufacture their values. Support both the short selector snapshot
 * (memory ordered) and the Linux four 32-bit zero-extended reads. */
unsigned int minic_x86_64_linux_segment_read_form(
    const char *text, size_t length) {
    static const char *const allowed[] = {
        "mov%%es,%0", "mov%%ds,%0",
        "movl%%fs,%0", "movl%%gs,%0", "movl%%ds,%0", "movl%%es,%0",
        "mov%%ss,%0", "mov%%cs,%0", "mov%%fs,%0", "mov%%gs,%0"
    };
    char normalized[48];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(normalized))
            return 0U;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(allowed)/sizeof(allowed[0]); ++i)
        if (strcmp(normalized, allowed[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_segment_read_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *dst;
    MinicType plain;
    const unsigned int form = source ? minic_x86_64_linux_segment_read_form(
        source->template_text, source->template_length) : 0U;
    const bool short_selector = form <= 2U || form >= 7U;
    if (!program || !source || !contract || source->is_volatile ||
        source->is_goto || !form ||
        source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count ||
        source->clobber_count != (short_selector ? 1U : 0U) ||
        source->has_memory_clobber != short_selector ||
        source->register_clobber_count ||
        !c_is(&source->outputs[0], "=r"))
        return false;
    dst = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!dst || dst->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(dst->type) ||
        !minic_type_unqualified(dst->type, &plain) ||
        !(minic_type_equal(plain, short_selector ?
              minic_type_unsigned_short() : minic_type_unsigned_int()) ||
          /* The kernel's i387 regset reader uses bare `mov %%ds,%0`
           * with an unsigned 32-bit fsave->fos lvalue. GAS accepts the
           * 32-bit GPR destination and zero-extends the selector; the C
           * storage width, not the bare assembly mnemonic, owns the store. */
          (form <= 2U && minic_type_equal(
              plain, minic_type_unsigned_int()))))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux loadsegment() is not the bare MOV to a segment register: its 1:/2:
 * fixup record uses exception type 5 and must survive as a three-word
 * __ex_table entry. Admit exactly that complete GNU spelling (including the
 * relocation base and exception handler type) for the 16-bit selector ISA. */
unsigned int minic_x86_64_linux_segment_load_extable_form(
    const char *text, size_t length) {
    static const char *const segment_names[] = {
        "fs", "gs", "ds", "es", "ss"
    };
    static const char tail[] =
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long5.popsection";
    char compact[180], expected[180];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(segment_names)/sizeof(segment_names[0]); ++i) {
        int written = snprintf(expected, sizeof(expected),
            "1:movw%%0,%%%%%s%s", segment_names[i], tail);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 1U;
    }
    return 0U;
}

static bool x86_linux_segment_load_extable_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *value;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_segment_load_extable_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto || source->output_count ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count != 1U ||
        !source->has_memory_clobber || source->register_clobber_count ||
        !c_is(&source->inputs[0], "rm"))
        return false;
    value = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!value || !minic_type_unqualified(value->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux 6.6 LKGS uses the fixed DI-register ISA encoding, with a full
 * __ex_table type-17 register fixup encoded by its GNU local macro. The
 * original 731-byte template is recognized whole: a modified macro,
 * relocated fault site or register number must not silently compile. */
bool minic_x86_64_linux_lkgs_extable_form(const char *text, size_t length) {
    static const char original[] =
        "1:  .byte 0xf2,0x0f,0x00,0xf7 ; "
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (1b) - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n"
        "extable_type_reg reg=%k[sel], type=(17 | ((0) << 16)) \n"
        ".purgem extable_type_reg\n"
        " .popsection\n";
    if (!text) return false;
    if (length == sizeof(original)-1U &&
        memcmp(text, original, sizeof(original)-1U) == 0)
        return true;
    /* Common Core resolves %[sel] to %0 while preserving the GNU %k
     * 32-bit register view and the rest of the exact exception macro. */
    if (length == sizeof(original)-5U) {
        static const char named[] = "%k[sel]";
        const char *slot = strstr(original, named);
        size_t prefix;
        if (!slot) return false;
        prefix = (size_t)(slot - original);
        return memcmp(text, original, prefix) == 0 &&
               memcmp(text + prefix, "%k0", 3U) == 0 &&
               memcmp(text + prefix + 3U, slot + sizeof(named)-1U,
                   (sizeof(original)-1U) - prefix - (sizeof(named)-1U)) == 0;
    }
    return false;
}

static bool x86_linux_lkgs_extable_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *selector;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_lkgs_extable_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !c_is(&source->outputs[0], "+D"))
        return false;
    selector = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!selector || selector->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(selector->type) ||
        !minic_type_unqualified(selector->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

/* FSGSBASE has four operand-bearing privileged forms; the read and write
 * halves have opposite C lvalue roles despite sharing a general GPR ISA
 * encoding. Preserve 64-bit width, memory sequencing and exact mnemonic. */
unsigned int minic_x86_64_linux_fsgsbase_form(
    const char *text, size_t length) {
    static const char *const exact[] = {
        "rdfsbase%0", "rdgsbase%0", "wrfsbase%0", "wrgsbase%0"
    };
    char compact[40];
    size_t n = 0U, i;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(exact)/sizeof(exact[0]); ++i)
        if (strcmp(compact, exact[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_fsgsbase_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_fsgsbase_form(
        source->template_text, source->template_length) : 0U;
    const bool reading = form <= 2U;
    const MinicInlineAsmOperand *operand;
    const MinicExpression *value;
    MinicType type;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        source->output_count != (reading ? 1U : 0U) ||
        source->input_count != (reading ? 0U : 1U) ||
        source->label_count || source->clobber_count != 1U ||
        !source->has_memory_clobber || source->register_clobber_count)
        return false;
    operand = reading ? (source->outputs ? &source->outputs[0] : NULL) :
        (source->inputs ? &source->inputs[0] : NULL);
    if (!operand || !c_is(operand, reading ? "=r" : "r")) return false;
    value = minic_c0_program_expression(program, operand->expression);
    if (!value || (reading && (value->value_category != MINIC_VALUE_LVALUE ||
                              minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = reading ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux kernel nofault MOV accesses have a three-word __ex_table record
 * whose second PREL32 word points to the C asm-goto fault label. They are
 * different from ordinary MOV and from uaccess __get_user() helpers.
 * Return 1..4 for load b/w/l/q and 5..8 for store b/w/l/q. */
unsigned int minic_x86_64_linux_kernel_nofault_goto_form(
    const char *text, size_t length) {
    static const char suffix[] =
        ".pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(%l2)-..long3.popsection";
    char compact[200], expected[200];
    size_t i, n = 0U, variant;
    static const char widths[] = "bwlq";
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (variant = 0U; variant < 8U; ++variant) {
        const bool load = variant < 4U;
        int written = snprintf(expected, sizeof(expected),
            "1:mov%c%s%s", widths[variant % 4U],
            load ? "%[umem],%[output]" : "%0,%1", suffix);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)variant + 1U;
        /* Core replaces GNU named operands with ordinal indices, but
         * retains the %l2 label placeholder for machine relocation. */
        if (load) {
            written = snprintf(expected, sizeof(expected),
                "1:mov%c%%1,%%0%s", widths[variant], suffix);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (unsigned int)variant + 1U;
        }
    }
    return 0U;
}

static bool x86_linux_kernel_nofault_goto_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ?
        minic_x86_64_linux_kernel_nofault_goto_form(
            source->template_text, source->template_length) : 0U;
    const bool load = form <= 4U;
    const unsigned int bytes = form ? 1U << ((form - 1U) % 4U) : 0U;
    const MinicInlineAsmOperand *data;
    const MinicInlineAsmOperand *memory;
    const MinicExpression *data_expr, *memory_expr;
    MinicType type;
    if (!program || !source || !contract || !form || !source->is_goto ||
        source->output_count != (load ? 1U : 0U) ||
        source->input_count != (load ? 1U : 2U) ||
        source->label_count != 1U || !source->labels ||
        source->clobber_count || source->register_clobber_count ||
        source->has_memory_clobber ||
        (load && (!source->outputs || !source->is_volatile)) ||
        !source->inputs)
        return false;
    data = load ? &source->outputs[0] : &source->inputs[0];
    memory = &source->inputs[load ? 0U : 1U];
    if (!target_constraint_is(data, load ?
            (bytes == 1U ? "=q" : "=r") :
            (bytes == 1U ? "iq" : bytes == 8U ? "er" : "ir")) ||
        !target_constraint_is(memory, "m"))
        return false;
    data_expr = minic_c0_program_expression(program, data->expression);
    memory_expr = minic_c0_program_expression(program, memory->expression);
    if (!data_expr || !memory_expr ||
        (load && (data_expr->value_category != MINIC_VALUE_LVALUE ||
                  minic_type_is_const(data_expr->type))) ||
        memory_expr->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(memory_expr->type) ||
        !minic_type_unqualified(data_expr->type, &type) ||
        (!minic_type_is_integer(type) &&
         !(bytes == 8U && minic_type_is_pointer(type))) ||
        (minic_type_is_integer(type) &&
         ((bytes == 1U && type.integer_rank != MINIC_INTEGER_RANK_CHAR) ||
          (bytes == 2U && type.integer_rank != MINIC_INTEGER_RANK_SHORT) ||
          (bytes == 4U && type.integer_rank != MINIC_INTEGER_RANK_INT) ||
          (bytes == 8U && type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                          type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = load ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux copy_user_generic couples REP MOVSB with the FSRM alternative CALL
 * and an exception-table recovery entry. The four GNU read/write operands
 * are RCX/RDI/RSI plus a fixed live RSP dependency; the replacement call
 * is not a normal C SysV call, and all relocations must survive intact. */
bool minic_x86_64_linux_rep_movsb_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "1:#ALT:oldnstr661:repmovsb662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(((1<<0)<<16)|((18*32+4)))"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:callrep_movs_alternative6651:.popsection"
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long3.popsection";
    char compact[560];
    size_t i, n = 0U;
    if (!text || length != 506U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* __clear_user() must preserve both the REP STOSB baseline and the feature-
 * guarded REP STOS helper. The RAX zero input and RSP fixed dependency are
 * part of this privileged ABI, as are the three __ex_table PREL32 words. */
bool minic_x86_64_linux_rep_stosb_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "1:#ALT:oldnstr661:repstosb662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(((1<<0)<<16)|((12*32+11)))"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:callrep_stos_alternative6651:.popsection"
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long3.popsection";
    char compact[560];
    size_t i, n = 0U;
    if (!text || length != 506U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

static bool x86_linux_rep_stosb_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const constraints[] = {"+c", "+D", "+r"};
    const MinicExpression *expr;
    const MinicFixedRegisterBinding *stack;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_rep_stosb_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 3U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0], "a"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (!target_constraint_is(&source->outputs[i], constraints[i]))
            return false;
        expr = minic_c0_program_expression(
            program, source->outputs[i].expression);
        if (!expr ||
            (i != 2U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))) ||
            (i == 1U ? !minic_type_is_pointer(expr->type) :
                       !minic_type_equal(expr->type, minic_type_unsigned_long())))
            return false;
    }
    expr = minic_c0_program_expression(
        program, source->outputs[2].expression);
    if (!expr) return false;
    if (expr->kind == MINIC_EXPRESSION_FIXED_REGISTER)
        stack = minic_c0_program_fixed_register_binding(
            program, expr->value.fixed_register_binding_id);
    else if (expr->kind == MINIC_EXPRESSION_LOCAL)
        stack = minic_c0_program_local_fixed_register_binding(
            program, expr->value.local_id);
    else return false;
    if (!stack || !stack->register_name ||
        strcmp(stack->register_name, "rsp") != 0)
        return false;
    expr = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!expr || !minic_type_is_integer(expr->type)) return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* FNSAVE/FWAIT, FXSAVE, FXSAVEQ, FXRSTOR, FXRSTORQ and FRSTOR in a user
 * signal frame are protected by STAC/CLAC alternative records as well as a
 * type-15 exception entry. An instruction-only matcher would make user
 * faults fatal in kernel mode; verify the two complete patch records. */
unsigned int minic_x86_64_linux_fpu_user_frame_form(
    const char *text, size_t length) {
    static const char alt_prefix[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(9*32+20)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:";
    static const char *const instructions[] = {
        "fnsave%[fx];fwait", "fxsave%[fx]", "fxsaveq%[fx]",
        "fxrstor%[fx]", "fxrstorq%[fx]", "frstor%[fx]",
        "fnsave%1;fwait", "fxsave%1", "fxsaveq%1",
        "fxrstor%1", "fxrstorq%1", "frstor%1",
        "fnsave%3;fwait", "fxsave%3", "fxsaveq%3",
        "fxrstor%3", "fxrstorq%3", "frstor%3"
    };
    static const char tail[] =
        ".pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long15.popsection";
    char compact[1100], expected[1100];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(instructions)/sizeof(instructions[0]); ++i) {
        const int wanted = snprintf(expected, sizeof(expected),
            "%s%s1:%s2:%s%s%s",
            alt_prefix, ".byte0x0f,0x01,0xcb6651:.popsection",
            instructions[i], alt_prefix,
            ".byte0x0f,0x01,0xca6651:.popsection", tail);
        if (wanted > 0 && (size_t)wanted < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)(i % 6U) + 1U;
    }
    return 0U;
}

static bool x86_linux_fpu_user_frame_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *error, *output, *input, *zero;
    const unsigned int form = source ?
        minic_x86_64_linux_fpu_user_frame_form(
            source->template_text, source->template_length) : 0U;
    MinicType plain;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        !target_constraint_is(&source->outputs[1], "=m") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "m"))
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    output = minic_c0_program_expression(
        program, source->outputs[1].expression);
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    input = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!error || !output || !zero || !input ||
        error->value_category != MINIC_VALUE_LVALUE ||
        output->value_category != MINIC_VALUE_LVALUE ||
        input->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_is_record(output->type) ||
        !minic_type_equal(output->type, input->type) ||
        !minic_type_is_integer(zero->type))
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

unsigned int minic_x86_64_linux_fxrstor_safe_form(
    const char *text, size_t length) {
    static const char extable[] =
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n";
    static const char *const opcodes[] = {"fxrstor", "fxrstorq"};
    char expected[800];
    size_t i;
    if (!text || length >= sizeof(expected)) return 0U;
    for (i = 0U; i < 4U; ++i) {
        const int written = snprintf(expected, sizeof(expected),
            "1:%s %s\n\t2:\n%s"
            "extable_type_reg reg=%s, type=(17 | ((-14) << 16)) \n"
            ".purgem extable_type_reg\n .popsection\n",
            opcodes[i % 2U], i < 2U ? "%[fx]" : "%3", extable,
            i < 2U ? "%[err]" : "%0");
        if (written > 0 && (size_t)written == length &&
            memcmp(text, expected, length) == 0)
            return (unsigned int)(i % 2U) + 1U;
    }
    return 0U;
}

bool minic_x86_64_linux_x87_fild_prepare_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "fnclexemmsfildl%[addr]", "fnclexemmsfildl%0"
    };
    char compact[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return true;
    return false;
}

/* MXCSR is a 32-bit memory control-state operand. Both the source's numeric
 * and named GNU operand spellings denote the same typed machine operation. */
unsigned int minic_x86_64_linux_mxcsr_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "ldmxcsr%0", "ldmxcsr%[val]", "stmxcsr%0", "stmxcsr%[val]"
    };
    char compact[80];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return i < 2U ? 1U : 2U;
    return 0U;
}

/* ORC unwinder snapshots the instruction pointer, current stack pointer and
 * frame pointer into three independent 64-bit GNU register outputs. These
 * are real hardware register reads; no fake C-call-frame addresses. */
bool minic_x86_64_linux_unwind_context_form(const char *text, size_t length) {
    static const char expected[] = "lea(%%rip),%0mov%%rsp,%1mov%%rbp,%2";
    char compact[128];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* Linux TASK_SIZE_MAX's CPU-feature alternative selects a 47-bit or 56-bit
 * user address ceiling. The replacement site and .altinstructions entry are
 * a single ABI contract. Literal `i` inputs are verified again against Core
 * integer constants at machine emission; arbitrary alternatives are denied. */
bool minic_x86_64_linux_task_size_alt_form(const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:movq%[small],%0662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(16*32+16)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:movq%[large],%06651:.popsection";
    static const char expected_numeric[] =
        "#ALT:oldnstr661:movq%2,%0662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(16*32+16)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:movq%3,%06651:.popsection";
    char compact[512];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0 ||
           strcmp(compact, expected_numeric) == 0;
}

static bool x86_linux_task_size_alt_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *output, *input;
    MinicType type;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_task_size_alt_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 3U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=r")) return false;
    output = minic_c0_program_expression(program, source->outputs[0].expression);
    if (!output || output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_unqualified(output->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_long())) return false;
    for (i = 0U; i < 3U; ++i) {
        if (!target_constraint_is(&source->inputs[i], "i")) return false;
        input = minic_c0_program_expression(program, source->inputs[i].expression);
        if (!input || !minic_type_is_integer(input->type)) return false;
    }
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    for (i = 1U; i < 4U; ++i)
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

static bool x86_linux_x87_fild_prepare_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *input;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 0U ||
        source->input_count != 1U || source->inputs == NULL ||
        source->label_count != 0U || source->clobber_count != 0U ||
        source->register_clobber_count != 0U || source->has_memory_clobber ||
        !minic_x86_64_linux_x87_fild_prepare_form(
            source->template_text, source->template_length) ||
        !target_constraint_is(&source->inputs[0], "m")) return false;
    input = minic_c0_program_expression(program, source->inputs[0].expression);
    if (!input || input->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(input->type)) return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* The kernel-state restore path is not the user-frame alternative and has
 * no destination error register.  The Linux exception table records type 6
 * (EX_TYPE_FPU_RESTORE); treating it as a naked memory read would remove the
 * kernel's recovery edge when a corrupt state is restored. */
unsigned int minic_x86_64_linux_fpu_kernel_restore_form(
    const char *text, size_t length) {
    static const char *const names[] = {"fxrstor", "fxrstorq", "frstor"};
    static const char *const operands[] = {"%[fx]", "%1"};
    static const char suffix[] =
        "2:.pushsection\"__ex_table\",\"a\".balign4"
        ".long(1b)-..long(2b)-..long6.popsection";
    char compact[256], expected[256];
    size_t i, j, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(names)/sizeof(names[0]); ++i) {
        for (j = 0U; j < sizeof(operands)/sizeof(operands[0]); ++j) {
            const int size = snprintf(expected, sizeof(expected),
                                      "1:%s%s%s", names[i], operands[j], suffix);
            if (size > 0 && (size_t)size < sizeof(expected) &&
                strcmp(expected, compact) == 0) return (unsigned int)i + 1U;
        }
    }
    return 0U;
}

static bool x86_linux_fpu_kernel_restore_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *output, *input;
    const MinicExpression *out_base, *in_base;
    const unsigned int form = source ? minic_x86_64_linux_fpu_kernel_restore_form(
        source->template_text, source->template_length) : 0U;
    if (program == NULL || source == NULL || contract == NULL || form == 0U ||
        !source->is_volatile || source->is_goto || source->output_count != 1U ||
        source->input_count != 1U || source->outputs == NULL ||
        source->inputs == NULL || source->label_count != 0U ||
        source->clobber_count != 0U || source->register_clobber_count != 0U ||
        source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=m") ||
        !target_constraint_is(&source->inputs[0], "m")) return false;
    output = minic_c0_program_expression(program, source->outputs[0].expression);
    input = minic_c0_program_expression(program, source->inputs[0].expression);
    if (output == NULL || input == NULL ||
        output->value_category != MINIC_VALUE_LVALUE ||
        input->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(output->type) ||
        !minic_type_equal(output->type, input->type) ||
        output->kind != MINIC_EXPRESSION_DEREFERENCE ||
        input->kind != MINIC_EXPRESSION_DEREFERENCE) return false;
    /* GCC's `=m` operand and paired `m` input describe one and the same
     * state object. Do not silently emit a read from a different object. */
    out_base = minic_c0_program_expression(program, output->value.unary.operand);
    in_base = minic_c0_program_expression(program, input->value.unary.operand);
    if (out_base == NULL || in_base == NULL ||
        out_base->kind != MINIC_EXPRESSION_LOCAL ||
        in_base->kind != MINIC_EXPRESSION_LOCAL ||
        out_base->value.local_id != in_base->value.local_id) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_fxrstor_safe_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *error, *memory, *zero, *input;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_fxrstor_safe_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->outputs[1], "=m") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "m"))
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    memory = minic_c0_program_expression(
        program, source->outputs[1].expression);
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    input = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!error || !memory || !zero || !input ||
        error->value_category != MINIC_VALUE_LVALUE ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        input->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_equal(error->type, minic_type_int()) ||
        !minic_type_is_record(memory->type) ||
        !minic_type_equal(memory->type, input->type) ||
        !minic_type_is_integer(zero->type))
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* The XSAVE/XSAVES and XRSTOR/XRSTORS families use an explicit %rdi memory
 * address, two fixed mask registers, and an EAX fault return. The exception
 * entry belongs to the instruction, not to the following XOR; preserving its
 * exact pair of PC-relative relocations is required for -EFAULT recovery. */
unsigned int minic_x86_64_linux_xstate_fault_form(
    const char *text, size_t length) {
    static const char *const opcodes[] = {
        "0x48,0x0f,0xae,0x27", /* XSAVE64 */
        "0x48,0x0f,0xae,0x2f", /* XRSTOR64 */
        "0x48,0x0f,0xc7,0x1f", /* XRSTORS64 */
        "0x48,0x0f,0xc7,0x2f"  /* XSAVES64 */
    };
    static const char suffix[] =
        "2:"
        ".pushsection\"__ex_table\",\"a\""
        ".balign4.long(1b)-..long(2b)-..long15.popsection";
    static const char *const error_operands[] = {
        "%[err]", "%0"
    };
    char compact[256], expected[256];
    size_t i, j, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(opcodes)/sizeof(opcodes[0]); ++i) {
        for (j = 0U; j < sizeof(error_operands)/
                           sizeof(error_operands[0]); ++j) {
            const int written = snprintf(expected, sizeof(expected),
                "1:.byte%sxor%s,%s%s", opcodes[i],
                error_operands[j], error_operands[j], suffix);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (unsigned int)i + 1U;
        }
    }
    return 0U;
}

static bool x86_linux_xstate_fault_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *err, *ptr, *mem, *low, *high;
    MinicType pointee, plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_xstate_fault_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 4U || !source->inputs ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        !target_constraint_is(&source->inputs[0], "D") ||
        !target_constraint_is(&source->inputs[1], "m") ||
        !target_constraint_is(&source->inputs[2], "a") ||
        !target_constraint_is(&source->inputs[3], "d"))
        return false;
    err = minic_c0_program_expression(program, source->outputs[0].expression);
    ptr = minic_c0_program_expression(program, source->inputs[0].expression);
    mem = minic_c0_program_expression(program, source->inputs[1].expression);
    low = minic_c0_program_expression(program, source->inputs[2].expression);
    high = minic_c0_program_expression(program, source->inputs[3].expression);
    if (!err || !ptr || !mem || !low || !high ||
        err->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(err->type) ||
        !minic_type_unqualified(err->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_pointee(ptr->type, &pointee) ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(mem->type) ||
        !minic_type_equal(mem->type, pointee) ||
        !minic_type_is_integer(low->type) ||
        !minic_type_is_integer(high->type))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* XRSTOR's kernel variant is ALTERNATIVE(XRSTOR64, XRSTORS64,
 * X86_FEATURE_XSAVES) plus a type-6 exception record. Both opcodes carry
 * an explicit fixed-RDI ModRM, and the patch section must be reproducible
 * exactly; otherwise the CPU can execute unsupported instructions. */
bool minic_x86_64_linux_xrstor_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:.byte0x48,0x0f,0xae,0x2f662:"
        "#ALT:padding.skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(10*32+3)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:.byte0x48,0x0f,0xc7,0x1f"
        "6651:.popsection3:"
        ".pushsection\"__ex_table\",\"a\".balign4"
        ".long(661b)-..long(3b)-..long6.popsection";
    char compact[570];
    size_t i, n = 0U;
    if (!text || length != 500U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

static bool x86_linux_xrstor_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *ptr, *mem, *low, *high;
    MinicType pointee;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_xrstor_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count || source->input_count != 4U ||
        !source->inputs || source->label_count ||
        source->register_clobber_count || source->clobber_count != 1U ||
        !source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0], "D") ||
        !target_constraint_is(&source->inputs[1], "m") ||
        !target_constraint_is(&source->inputs[2], "a") ||
        !target_constraint_is(&source->inputs[3], "d"))
        return false;
    ptr = minic_c0_program_expression(program, source->inputs[0].expression);
    mem = minic_c0_program_expression(program, source->inputs[1].expression);
    low = minic_c0_program_expression(program, source->inputs[2].expression);
    high = minic_c0_program_expression(program, source->inputs[3].expression);
    if (!ptr || !mem || !low || !high ||
        !minic_type_pointee(ptr->type, &pointee) ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(mem->type) ||
        !minic_type_equal(mem->type, pointee) ||
        !minic_type_is_integer(low->type) ||
        !minic_type_is_integer(high->type))
        return false;
    contract->operand_count = 4U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* The kernel XSAVE ALTERNATIVE_3 is a 2048-byte GNU macro expansion with
 * three nested maximum-length expressions, three CPU patch records, and the
 * full type-17 register-recovery .irp macro. Comparing a *complete* compact
 * fingerprint, rather than selected opcodes, prevents a malformed .skip or
 * wrong CPU feature from being silently accepted. FNV-1a is used only as a
 * frozen exact-template identity check, never on untrusted executable data.
 * The second form is the Core canonicalization of all three %[err] uses to
 * %0. Both variants represent the same GNU template and operands. */
bool minic_x86_64_linux_xsave_triple_alternative_form(
    const char *text, size_t length) {
    unsigned long long digest = 14695981039346656037ULL;
    size_t i, compact = 0U;
    if (!text || (length != 2048U && length != 2036U)) return false;
    for (i = 0U; i < length; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c > 127U) return false;
        digest = (digest ^ c) * 1099511628211ULL;
        ++compact;
    }
    return (compact == 1768U && digest == 0x25454d8338fd3d59ULL) ||
           (compact == 1756U && digest == 0xcc61d8e371e750ccULL);
}

static bool x86_linux_xsave_triple_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *error, *ptr, *mem, *low, *high;
    MinicType pointee, plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_xsave_triple_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 4U || !source->inputs ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->inputs[0], "D") ||
        !target_constraint_is(&source->inputs[1], "m") ||
        !target_constraint_is(&source->inputs[2], "a") ||
        !target_constraint_is(&source->inputs[3], "d"))
        return false;
    error = minic_c0_program_expression(program, source->outputs[0].expression);
    ptr = minic_c0_program_expression(program, source->inputs[0].expression);
    mem = minic_c0_program_expression(program, source->inputs[1].expression);
    low = minic_c0_program_expression(program, source->inputs[2].expression);
    high = minic_c0_program_expression(program, source->inputs[3].expression);
    if (!error || !ptr || !mem || !low || !high ||
        error->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !minic_type_pointee(ptr->type, &pointee) ||
        mem->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(mem->type) ||
        !minic_type_equal(mem->type, pointee) ||
        !minic_type_is_integer(low->type) ||
        !minic_type_is_integer(high->type))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* jump_label's patch site is *not* merely a JMP: Linux scans a writable
 * __jump_table of (site PREL32, target PREL32, key+flags PREL64). The bit
 * test in the third word must be emitted at assembly time, never evaluated
 * through a runtime pointer. Keep both GNU asm-goto spellings and their
 * named/numeric label placeholders as complete templates. */
unsigned int minic_x86_64_linux_jump_label_form(
    const char *text, size_t length) {
    static const char shared[] =
        ".pushsection__jump_table,\"aw\".balign8"
        ".long1b-..long%s-..quad%%c0+%%c1-..popsection";
    static const char *const jump_spellings[] = {
        "jmp%l[l_yes]#objtoolNOPsthis", "jmp%l[l_yes]",
        "jmp%l2#objtoolNOPsthis", "jmp%l2"
    };
    char compact[220], expected[220], table[180];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 4U; ++i) {
        const char *label = i < 2U ? "%l[l_yes]" : "%l2";
        const int table_len = snprintf(table, sizeof(table), shared, label);
        const int wanted = snprintf(expected, sizeof(expected),
                                    "1:%s%s", jump_spellings[i], table);
        if (table_len > 0 && (size_t)table_len < sizeof(table) &&
            wanted > 0 && (size_t)wanted < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return (unsigned int)(i % 2U) + 1U;
    }
    return 0U;
}

static bool x86_linux_jump_label_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *key, *flags;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_jump_label_form(
            source->template_text, source->template_length) ||
        !source->is_goto || source->is_volatile ||
        source->output_count || source->input_count != 2U ||
        !source->inputs || source->label_count != 1U || !source->labels ||
        source->clobber_count || source->register_clobber_count ||
        source->has_memory_clobber ||
        !target_constraint_is(&source->inputs[0], "i") ||
        !target_constraint_is(&source->inputs[1], "i"))
        return false;
    key = minic_c0_program_expression(program, source->inputs[0].expression);
    flags = minic_c0_program_expression(program, source->inputs[1].expression);
    if (!key || !flags || !minic_type_is_pointer(key->type) ||
        !minic_type_is_integer(flags->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Protection Keys form a two-instruction hardware interface, not a textual
 * GNU passthrough. Linux's special_insns.h uses EAX/ECX/EDX for WRPKRU and
 * two distinct write-only EAX/EDX results for RDPKRU. An incorrect operand
 * order or size would change the active userspace page-access permissions;
 * admit only the exact kernel byte encodings and fixed GPR constraints. */
unsigned int minic_x86_64_linux_pkru_form(
    const char *text, size_t length) {
    char compact[96];
    size_t i, n = 0U;
    if (!text || length < 12U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strcmp(compact, ".byte0x0f,0x01,0xee") == 0) return 1U;
    if (strcmp(compact, ".byte0x0f,0x01,0xef") == 0) return 2U;
    return 0U;
}

static bool x86_linux_pkru_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_pkru_form(
        source->template_text, source->template_length) : 0U;
    const bool read = form == 1U;
    static const char *const input_regs[] = {"a", "c", "d"};
    static const char *const output_regs[] = {"=a", "=d"};
    const MinicExpression *expression;
    MinicType value_type;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->clobber_count || source->register_clobber_count ||
        source->has_memory_clobber ||
        source->output_count != (read ? 2U : 0U) ||
        source->input_count != (read ? 1U : 3U) ||
        (read && !source->outputs) || !source->inputs)
        return false;
    for (i = 0U; i < source->output_count; ++i) {
        if (!target_constraint_is(&source->outputs[i], output_regs[i]) ||
            source->outputs[i].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)
            return false;
        expression = minic_c0_program_expression(
            program, source->outputs[i].expression);
        if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(expression->type) ||
            !minic_type_unqualified(expression->type, &value_type) ||
            !minic_type_equal(value_type, minic_type_unsigned_int()))
            return false;
        contract->kinds[i] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    }
    for (i = 0U; i < source->input_count; ++i) {
        const size_t index = source->output_count + i;
        const char *expected = read ? "c" : input_regs[i];
        if (!target_constraint_is(&source->inputs[i], expected) ||
            source->inputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY)
            return false;
        expression = minic_c0_program_expression(
            program, source->inputs[i].expression);
        if (!expression ||
            !minic_type_unqualified(expression->type, &value_type) ||
            !minic_type_equal(value_type, minic_type_unsigned_int()))
            return false;
        contract->kinds[index] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = source->output_count + source->input_count;
    return true;
}

/* The historic Pentium FDIV probe is still built by Linux 6.6.143. It
 * combines three C memory objects and x87 operations that cannot be
 * modelled as GPR arithmetic. Admitting the exact multi-operation template
 * as a target-owned contract preserves FPU state resets, rounding of FISTP
 * to an int destination, and the fwait ordering barrier. */
bool minic_x86_64_linux_fdiv_bug_form(
    const char *text, size_t length) {
    static const char expected[] =
        "fninitfldl%1fdivl%2fmull%2fldl%1"
        "fsubp%%st,%%st(1)fistpl%0fwaitfninit";
    char normalized[200];
    size_t i, n = 0U;
    if (!text || length < 40U || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = ch;
    }
    normalized[n] = '\0';
    return strcmp(normalized, expected) == 0;
}

static bool x86_linux_fdiv_bug_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expression;
    MinicType type;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_fdiv_bug_form(
            source->template_text, source->template_length) ||
        source->is_goto || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        source->output_count != 1U || source->input_count != 2U ||
        !source->outputs || !source->inputs ||
        !target_constraint_is(&source->outputs[0], "=m") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY)
        return false;
    expression = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(expression->type) ||
        !minic_type_unqualified(expression->type, &type) ||
        !minic_type_equal(type, minic_type_int()))
        return false;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
    for (i = 0U; i < 2U; ++i) {
        if (!target_constraint_is(&source->inputs[i], "m") ||
            source->inputs[i].access != MINIC_INLINE_ASM_OPERAND_READ_ONLY)
            return false;
        expression = minic_c0_program_expression(
            program, source->inputs[i].expression);
        if (!expression || expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(expression->type, &type) ||
            !minic_type_equal(type, minic_type_double()))
            return false;
        contract->kinds[i + 1U] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    }
    contract->operand_count = 3U;
    contract->implicit_memory_effect = true;
    return true;
}

/* Four appearances of Linux's SMAP ALTERNATIVE macro share the exact
 * static-key feature and relocation layout, but have distinct observable
 * state changes. A template is admissible only with the complete section
 * metadata, feature 9*32+20, and precisely one of the four replacement
 * instruction bodies. Removing the patch metadata must never silently
 * turn a conditional STAC/CLAC into an unconditional privileged opcode. */
unsigned int minic_x86_64_linux_smap_alternative_form(
    const char *text, size_t length) {
    static const char shared[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(9*32+20)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:";
    static const char *const prefixes[] = {
        "", "", "#smap_save", "#smap_restore"
    };
    static const char *const bodies[] = {
        ".byte0x0f,0x01,0xca", ".byte0x0f,0x01,0xcb",
        "pushf;pop%0;.byte0x0f,0x01,0xca", "push%0;popf"
    };
    char compact[700], expected[700];
    size_t i, n = 0U;
    unsigned int form;
    if (!text || length > 680U || length < 300U) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (form = 1U; form <= 4U; ++form) {
        const int required = snprintf(expected, sizeof(expected), "%s%s%s%s",
            prefixes[form - 1U], shared, bodies[form - 1U],
            "6651:.popsection");
        if (required > 0 && (size_t)required < sizeof(expected) &&
            strcmp(compact, expected) == 0)
            return form;
    }
    return 0U;
}

/* Mitigation switch in user_access_begin(): an empty sequence is replaced
 * with LFENCE iff X86_FEATURE_USERCOPY_FENCE (20*32+2) is present. The
 * alternative lengths, feature, labels and ELF sections are indivisible. */
bool minic_x86_64_linux_lfence_alternative_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(20*32+2)"
        ".byte663b-661b.byte6651f-6641f.popsection"
        ".pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:lfence6651:.popsection";
    char compact[420];
    size_t i, count = 0U;
    if (!text || length != 351U) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || count + 1U >= sizeof(compact)) return false;
        compact[count++] = c;
    }
    compact[count] = '\0';
    return strcmp(compact, expected) == 0;
}

/* Linux strnlen/strscpy fast path: faulting word read, followed by a
 * type-20 exception-table record. This is NOT the generic nofault handler
 * (type 3): the exception handler completes the part-before-page-boundary
 * word and zeros the remaining inaccessible bytes in the result register. */
bool minic_x86_64_linux_load_unaligned_zeropad_form(
    const char *text, size_t length) {
    static const char expected[] =
        "1:\tmov %[mem], %[ret]\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 20 \n"
        " .popsection\n";
    static const char numeric[] =
        "1:\tmov %1, %0\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 20 \n"
        " .popsection\n";
    return text &&
        ((length == sizeof(expected) - 1U &&
          memcmp(text, expected, length) == 0) ||
         (length == sizeof(numeric) - 1U &&
          memcmp(text, numeric, length) == 0));
}

static bool x86_linux_load_unaligned_zeropad_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *output, *memory;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_load_unaligned_zeropad_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->inputs[0], "m"))
        return false;
    output = minic_c0_program_expression(
        program, source->outputs[0].expression);
    memory = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!output || !memory ||
        output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(output->type) ||
        !minic_type_equal(output->type, minic_type_unsigned_long()) ||
        memory->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_equal(memory->type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux x86-64 signal frame sanitization uses LAR (Load Access Rights)
 * to inspect a user SS selector. An invalid selector clears ZF; the C
 * output must then be zero, while a valid selector exposes its access
 * rights. Model the instruction and its ZF branch together. */
bool minic_x86_64_linux_lar_selector_form(
    const char *text, size_t length) {
    static const char source[] =
        "lar %[old_ss], %[ar]\n\t"
        "jz 1f\n\t"
        "xorl %[ar], %[ar]\n\t"
        "1:";
    static const char numeric[] =
        "lar %1, %0\n\t"
        "jz 1f\n\t"
        "xorl %0, %0\n\t"
        "1:";
    return text &&
        ((length == sizeof(source) - 1U &&
          memcmp(text, source, length) == 0) ||
         (length == sizeof(numeric) - 1U &&
          memcmp(text, numeric, length) == 0));
}

static bool x86_linux_lar_selector_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *rights, *selector;
    MinicType plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_lar_selector_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=r") ||
        !target_constraint_is(&source->inputs[0], "rm"))
        return false;
    rights = minic_c0_program_expression(
        program, source->outputs[0].expression);
    selector = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!rights || !selector ||
        rights->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(rights->type) ||
        !minic_type_equal(rights->type, minic_type_unsigned_int()) ||
        !minic_type_unqualified(selector->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}

/* GNU flag output =@ccc observes the hardware Carry Flag, not whether
 * the generated random word happens to be nonzero. RDRAND and RDSEED
 * share the same operand ABI but advertise distinct CPU feature bits. */
unsigned int minic_x86_64_linux_random_seed_form(
    const char *text, size_t length) {
    static const char *const originals[] = {
        "rdrand %[out]\n\t/* output condition code c*/\n",
        "rdseed %[out]\n\t/* output condition code c*/\n",
        "rdrand %1\n\t/* output condition code c*/\n",
        "rdseed %1\n\t/* output condition code c*/\n"
    };
    size_t i;
    if (!text) return 0U;
    for (i = 0U; i < sizeof(originals) / sizeof(originals[0]); ++i)
        if (length == strlen(originals[i]) &&
            memcmp(text, originals[i], length) == 0)
            return (unsigned int)(i % 2U) + 1U;
    return 0U;
}

static bool x86_linux_random_seed_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *flag, *output;
    MinicType plain;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_random_seed_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 2U || !source->outputs ||
        source->input_count || source->label_count || source->clobber_count ||
        source->has_memory_clobber || source->register_clobber_count ||
        !target_constraint_is(&source->outputs[0], "=@ccc") ||
        !target_constraint_is(&source->outputs[1], "=r"))
        return false;
    flag = minic_c0_program_expression(
        program, source->outputs[0].expression);
    output = minic_c0_program_expression(
        program, source->outputs[1].expression);
    if (!flag || !output ||
        flag->value_category != MINIC_VALUE_LVALUE ||
        output->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(flag->type) || minic_type_is_const(output->type) ||
        !minic_type_unqualified(flag->type, &plain) ||
        !minic_type_equal(plain, minic_type_bool()) ||
        !minic_type_unqualified(output->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    return true;
}

static bool x86_linux_smap_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expression;
    const unsigned int form = source ?
        minic_x86_64_linux_smap_alternative_form(
            source->template_text, source->template_length) : 0U;
    const MinicInlineAsmOperand *operand;
    if (!program || !source || !contract || (form != 3U && form != 4U) ||
        !source->is_volatile || source->is_goto || source->label_count ||
        !source->has_memory_clobber || source->clobber_count != 2U ||
        source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "cc") ||
        source->output_count != (form == 3U ? 1U : 0U) ||
        source->input_count != (form == 4U ? 1U : 0U))
        return false;
    operand = form == 3U ? &source->outputs[0] : &source->inputs[0];
    if (!target_constraint_is(operand, form == 3U ? "=rm" : "g"))
        return false;
    expression = minic_c0_program_expression(program, operand->expression);
    if (!expression ||
        (form == 3U && (expression->value_category != MINIC_VALUE_LVALUE ||
                        minic_type_is_const(expression->type))) ||
        !minic_type_equal(expression->type, minic_type_unsigned_long()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = form == 3U ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_rep_movsb_alternative_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    static const char *const constraints[] = {"+c", "+D", "+S", "+r"};
    const MinicExpression *expression;
    const MinicFixedRegisterBinding *stack_binding;
    size_t i;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_rep_movsb_alternative_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 4U || !source->outputs ||
        source->input_count || source->label_count ||
        source->clobber_count != 2U || !source->has_memory_clobber ||
        source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "rax"))
        return false;
    for (i = 0U; i < 4U; ++i) {
        if (!target_constraint_is(&source->outputs[i], constraints[i]))
            return false;
        expression = minic_c0_program_expression(
            program, source->outputs[i].expression);
        if (!expression ||
            (i != 3U && expression->value_category != MINIC_VALUE_LVALUE) ||
            (i != 3U && minic_type_is_const(expression->type)) ||
            (i == 0U || i == 3U ?
                !minic_type_equal(expression->type, minic_type_unsigned_long()) :
                !minic_type_is_pointer(expression->type)))
            return false;
    }
    expression = minic_c0_program_expression(
        program, source->outputs[3].expression);
    if (!expression) return false;
    if (expression->kind == MINIC_EXPRESSION_FIXED_REGISTER)
        stack_binding = minic_c0_program_fixed_register_binding(
            program, expression->value.fixed_register_binding_id);
    else if (expression->kind == MINIC_EXPRESSION_LOCAL)
        stack_binding = minic_c0_program_local_fixed_register_binding(
            program, expression->value.local_id);
    else return false;
    if (!stack_binding || !stack_binding->register_name ||
        strcmp(stack_binding->register_name, "rsp") != 0)
        return false;
    contract->operand_count = 4U;
    for (i = 0U; i < 4U; ++i)
        contract->kinds[i] =
            i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                      MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

/* The exception-table register helper in Linux loadsegment(ES/DS/SS) is
 * distinct from the short-selector FS fixup. Its type-17 entry names the
 * GPR that held the selector so the exception handler can rewrite it.
 * Check the whole expanded GNU macro, including its register-number table. */
unsigned int minic_x86_64_linux_segment_register_extable_form(
    const char *text, size_t length) {
    static const char body[] =
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (1b) - .\n"
        ".macro extable_type_reg type:req reg:req\n"
        ".set .Lfound, 0\n"
        ".set .Lregnr, 0\n"
        ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".set .Lregnr, 0\n"
        ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"
        ".ifc \\reg, %%\\rs\n"
        ".set .Lfound, .Lfound+1\n"
        ".long \\type + (.Lregnr << 8)\n"
        ".endif\n"
        ".set .Lregnr, .Lregnr+1\n"
        ".endr\n"
        ".if (.Lfound != 1)\n"
        ".error \"extable_type_reg: bad register argument\"\n"
        ".endif\n"
        ".endm\n"
        "extable_type_reg reg=%k0, type=(17 | ((0) << 16)) \n"
        ".purgem extable_type_reg\n"
        " .popsection\n";
    static const char *const names[] = {"es", "ds", "ss"};
    char exact[780];
    size_t i;
    if (!text || length != 721U) return 0U;
    for (i = 0U; i < sizeof(names)/sizeof(names[0]); ++i) {
        const int written = snprintf(exact, sizeof(exact),
            "\t\t\t\t\t\t\n1:\tmovl %%k0,%%%%%s\t\t\n%s",
            names[i], body);
        if (written > 0 && (size_t)written == length &&
            memcmp(text, exact, length) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}

static bool x86_linux_segment_register_extable_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *selector;
    MinicType type;
    if (!program || !source || !contract ||
        !minic_x86_64_linux_segment_register_extable_form(
            source->template_text, source->template_length) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 1U || !source->outputs ||
        source->input_count || source->label_count ||
        source->clobber_count != 1U || !source->has_memory_clobber ||
        source->register_clobber_count ||
        !c_is(&source->outputs[0], "+r"))
        return false;
    selector = minic_c0_program_expression(
        program, source->outputs[0].expression);
    if (!selector || selector->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(selector->type) ||
        !minic_type_unqualified(selector->type, &type) ||
        !minic_type_equal(type, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->implicit_memory_effect = true;
    return true;
}

/* Native 128-bit CMPXCHG is a single 16-byte atomic read/conditional write;
 * the ZF output and SMP lock-site relocation are observable Linux effects.
 * Match complete source and numbered-Core templates, not a substring that
 * might accidentally admit an unrelated assembly program. */
unsigned int minic_x86_64_linux_cmpxchg128_form(const char *text, size_t length) {
    static const char head[] =
        ".pushsection .smp_locks,\"a\"\n"
        ".balign 4\n"
        ".long 671f - .\n"
        ".popsection\n"
        "671:\n\tlock; cmpxchg16b ";
    static const char tail[] = "\n\t/* output condition code e*/\n";
    static const char *const operand[] = {"%[ptr]", "%1"};
    static const char alt_head[] =
        "# ALT: oldnstr\n"
        "661:\n\tleaq ";
    static const char alt_middle[] =
        ", %%rsi; call this_cpu_cmpxchg16b_emu\n"
        "662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
            "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long 6641f - .\n"
        " .4byte ( 4*32+13)\n"
        " .byte 663b-661b\n"
        " .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\tcmpxchg16b %%gs:";
    static const char alt_tail[] = "\n6651:\n.popsection\n";
    static const char *const alt_address[][2] = {
        {"%P[var]", "%[var]"}, {"%P0", "%0"}
    };
    size_t i;
    if (!text || !length || length > 512U || strlen(text) != length) return 0U;
    for (i = 0U; i < sizeof(operand)/sizeof(operand[0]); ++i) {
        const size_t p = sizeof(head)-1U, o = strlen(operand[i]);
        const size_t t = sizeof(tail)-1U;
        if (length == p+o+t && memcmp(text, head, p)==0 &&
            memcmp(text+p, operand[i], o)==0 &&
            memcmp(text+p+o, tail, t)==0)
            return 1U;
    }
    for (i=0U;i<sizeof(alt_address)/sizeof(alt_address[0]);++i) {
        const size_t h=sizeof(alt_head)-1U, m=sizeof(alt_middle)-1U;
        const size_t t=sizeof(alt_tail)-1U;
        const size_t a=strlen(alt_address[i][0]), b=strlen(alt_address[i][1]);
        if (length == h+a+m+b+t && memcmp(text,alt_head,h)==0 &&
            memcmp(text+h,alt_address[i][0],a)==0 &&
            memcmp(text+h+a,alt_middle,m)==0 &&
            memcmp(text+h+a+m,alt_address[i][1],b)==0 &&
            memcmp(text+h+a+m+b,alt_tail,t)==0)
            return 2U;
    }
    return 0U;
}

static bool x86_linux_cmpxchg128_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_cmpxchg128_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *cc, *memory, *low, *high, *new_low, *new_high;
    const bool alt = form == 2U;
    const size_t at = alt ? 0U : 1U;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto || source->label_count ||
        source->output_count != (alt ? 3U : 4U) || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        !source->has_memory_clobber ||
        source->clobber_count != (alt ? 2U : 1U) ||
        source->register_clobber_count != (alt ? 1U : 0U) ||
        (alt && (!source->register_clobbers ||
                 !clobber_is(&source->register_clobbers[0], "rsi"))) ||
        (!alt && !target_constraint_is(&source->outputs[0], "=@cce")) ||
        !target_constraint_is(&source->outputs[at], "+m") ||
        !target_constraint_is(&source->outputs[at+1U], "+a") ||
        !target_constraint_is(&source->outputs[at+2U], "+d") ||
        !target_constraint_is(&source->inputs[0], "b") ||
        !target_constraint_is(&source->inputs[1], "c") ||
        !source->outputs[at].name ||
        source->outputs[at].name_length != 3U ||
        memcmp(source->outputs[at].name, alt ? "var" : "ptr", 3U) != 0)
        return false;
    cc = alt ? NULL : minic_c0_program_expression(
        program, source->outputs[0].expression);
    memory = minic_c0_program_expression(program, source->outputs[at].expression);
    low = minic_c0_program_expression(program, source->outputs[at+1U].expression);
    high = minic_c0_program_expression(program, source->outputs[at+2U].expression);
    new_low = minic_c0_program_expression(program, source->inputs[0].expression);
    new_high = minic_c0_program_expression(program, source->inputs[1].expression);
    if ((!alt && (!cc || cc->value_category != MINIC_VALUE_LVALUE ||
                  !minic_type_is_integer(cc->type) ||
                  cc->type.integer_rank != MINIC_INTEGER_RANK_BOOL)) ||
        !memory || memory->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(memory->type) ||
        !minic_type_is_integer(memory->type) ||
        memory->type.integer_rank != MINIC_INTEGER_RANK_INT128)
        return false;
    {
        const MinicExpression *parts[] = {low, high, new_low, new_high};
        for (i=0U;i<4U;++i)
            if (!parts[i] || !minic_type_is_integer(parts[i]->type) ||
                (i<2U && (parts[i]->value_category != MINIC_VALUE_LVALUE ||
                          minic_type_is_const(parts[i]->type))) ||
                (parts[i]->type.integer_rank != MINIC_INTEGER_RANK_LONG &&
                 parts[i]->type.integer_rank != MINIC_INTEGER_RANK_LONG_LONG))
                return false;
    }
    contract->operand_count = alt ? 5U : 6U;
    if (!alt)
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[at] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[at+1U] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[at+2U] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    contract->kinds[at+3U] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[at+4U] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* __put_user_size() is a Linux kernel exception-table-aware helper CALL,
 * not a C store: its input ABI is RCX=user address, RAX=scalar value,
 * RSP=current live stack, ECX=error result. The GNU EBX clobber is part of
 * this private ABI and cannot be dropped just because SysV usually saves
 * RBX. Match the full source constraint footprint and original named size
 * placeholder; Core resolves that size to one integer at a call site. */
unsigned int minic_x86_64_linux_put_user_call_form(
    const char *text, size_t length) {
    static const char original[] = "call__put_user_%c[size]";
    static const char *const lowered[] = {
        "call__put_user_1", "call__put_user_2",
        "call__put_user_4", "call__put_user_8"
    };
    char normalized[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(normalized)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(normalized))
            return 0U;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    if (strcmp(normalized, original) == 0) return 0x100U;
    if (strcmp(normalized, "call__put_user_nocheck_%c[size]") == 0)
        return 0x400U;
    /* Common Core resolves the named %[size] to a numbered immediate
     * binding but deliberately leaves GNU %c for the target to encode. */
    if (strcmp(normalized, "call__put_user_%c4") == 0) return 0x200U;
    if (strcmp(normalized, "call__put_user_nocheck_%c4") == 0)
        return 0x800U;
    for (i = 0U; i < sizeof(lowered)/sizeof(lowered[0]); ++i)
        if (strcmp(normalized, lowered[i]) == 0)
            return (unsigned int)(1U << i);
    return 0U;
}

static bool x86_linux_put_user_call_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_put_user_call_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *error, *stack, *user, *value, *size;
    const MinicFixedRegisterBinding *binding;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || (form != 0x100U && form != 0x400U) ||
        source->output_count != 2U || !source->outputs ||
        source->input_count != 3U || !source->inputs || source->label_count ||
        source->clobber_count != 1U || source->has_memory_clobber ||
        source->register_clobber_count != 1U || !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "ebx") ||
        !target_constraint_is(&source->outputs[0], "=c") ||
        !target_constraint_is(&source->outputs[1], "+r") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "r") ||
        !target_constraint_is(&source->inputs[2], "i") ||
        !source->inputs[2].name || source->inputs[2].name_length != 4U ||
        memcmp(source->inputs[2].name, "size", 4U) != 0)
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    stack = minic_c0_program_expression(
        program, source->outputs[1].expression);
    user = minic_c0_program_expression(
        program, source->inputs[0].expression);
    value = minic_c0_program_expression(
        program, source->inputs[1].expression);
    size = minic_c0_program_expression(
        program, source->inputs[2].expression);
    if (!error || error->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !stack || stack->kind != MINIC_EXPRESSION_FIXED_REGISTER ||
        !minic_type_is_integer(stack->type) ||
        !user || !minic_type_is_pointer(user->type) ||
        /* GNU __put_user_8 stores either an integer payload or an address.
         * Linux's robust-list/sigaltstack writers load C object pointers
         * in the same mandatory RAX register and preserve all 64 bits. */
        !value || (!minic_type_is_integer(value->type) &&
                   !minic_type_is_pointer(value->type)) ||
        !size || !minic_type_is_integer(size->type))
        return false;
    binding = minic_c0_program_fixed_register_binding(
        program, stack->value.fixed_register_binding_id);
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "rsp") != 0)
        return false;
    /* The source's explicit RAX binding is material: this private helper
     * does not use the ordinary System V argument register allocator. */
    if (value->kind == MINIC_EXPRESSION_FIXED_REGISTER)
        binding = minic_c0_program_fixed_register_binding(
            program, value->value.fixed_register_binding_id);
    else if (value->kind == MINIC_EXPRESSION_LOCAL)
        binding = minic_c0_program_local_fixed_register_binding(
            program, value->value.local_id);
    else
        return false;
    if (!binding || !binding->register_name ||
        (strcmp(binding->register_name, "rax") != 0 &&
         strcmp(binding->register_name, "%rax") != 0))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux __get_user_size() / __get_user_nocheck_size() are native uaccess
 * helpers with error in EAX and read value in RDX, opposite to put_user's
 * RCX pointer convention. RSP remains a live fixed-register tied operand.
 * The body contains its own exception-table fixup; no plain load may replace
 * the fault-aware CALL. Preserve the named GNU immediate until Core resolves
 * the actual width at the call site. */
unsigned int minic_x86_64_linux_get_user_call_form(
    const char *text, size_t length) {
    static const char *const names[] = {
        "call__get_user_%c[size]",
        "call__get_user_nocheck_%c[size]",
        "call__get_user_%c4",
        "call__get_user_nocheck_%c4"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(names)/sizeof(names[0]); ++i)
        if (strcmp(compact, names[i]) == 0)
            return (unsigned int)i + 1U;
    return 0U;
}

static bool x86_linux_get_user_call_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_get_user_call_form(
        source->template_text, source->template_length) : 0U;
    const MinicExpression *error, *value, *stack, *pointer, *size;
    const MinicFixedRegisterBinding *binding;
    MinicType plain;
    if (!program || !source || !contract || (form != 1U && form != 2U) ||
        !source->is_volatile || source->is_goto ||
        source->output_count != 3U || !source->outputs ||
        source->input_count != 2U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !target_constraint_is(&source->outputs[0], "=a") ||
        !target_constraint_is(&source->outputs[1], "=r") ||
        !target_constraint_is(&source->outputs[2], "+r") ||
        !target_constraint_is(&source->inputs[0], "0") ||
        !target_constraint_is(&source->inputs[1], "i") ||
        !source->inputs[1].name || source->inputs[1].name_length != 4U ||
        memcmp(source->inputs[1].name, "size", 4U) != 0)
        return false;
    error = minic_c0_program_expression(
        program, source->outputs[0].expression);
    value = minic_c0_program_expression(
        program, source->outputs[1].expression);
    stack = minic_c0_program_expression(
        program, source->outputs[2].expression);
    pointer = minic_c0_program_expression(
        program, source->inputs[0].expression);
    size = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!error || error->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(error->type) ||
        !minic_type_unqualified(error->type, &plain) ||
        !minic_type_equal(plain, minic_type_int()) ||
        !value || value->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(value->type) || !minic_type_is_integer(value->type) ||
        !stack || stack->kind != MINIC_EXPRESSION_FIXED_REGISTER ||
        !minic_type_is_integer(stack->type) ||
        !pointer || !minic_type_is_pointer(pointer->type) ||
        !size || !minic_type_is_integer(size->type))
        return false;
    binding = minic_c0_program_fixed_register_binding(
        program, stack->value.fixed_register_binding_id);
    if (!binding || !binding->register_name ||
        strcmp(binding->register_name, "rsp") != 0)
        return false;
    if (value->kind == MINIC_EXPRESSION_FIXED_REGISTER)
        binding = minic_c0_program_fixed_register_binding(
            program, value->value.fixed_register_binding_id);
    else if (value->kind == MINIC_EXPRESSION_LOCAL)
        binding = minic_c0_program_local_fixed_register_binding(
            program, value->value.local_id);
    else
        return false;
    if (!binding || !binding->register_name ||
        (strcmp(binding->register_name, "rdx") != 0 &&
         strcmp(binding->register_name, "%rdx") != 0))
        return false;
    contract->operand_count = 5U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux x86 idle.c CLFLUSH alternative: the ordinary instruction stream
 * contains only NOP padding, while .altinstr_replacement holds a CLFLUSH
 * against a physical RAX pointer. Feature 22*32+7 selects this replacement.
 * Preserve the EXACT Linux local labels, MAX-length .skip arithmetic,
 * PREL32 records and instruction-size bytes. This is not a plain prefetch,
 * and it is not safe to drop the alternative when the feature is absent. */
bool minic_x86_64_linux_idle_clflush_alt_form(
    const char *text, size_t length, bool core_numeric) {
    static const char prefix[] =
        "#ALT:oldnstr661:662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(22*32+(7))"
        ".byte663b-661b.byte6651f-6641f"
        ".popsection.pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:clflush(";
    static const char suffix[] = ")6651:.popsection";
    static const char *const operand[] = {"%[addr]", "%1"};
    char compact[512];
    char expected[512];
    size_t i, n = 0U;
    const int count = snprintf(expected, sizeof(expected), "%s%s%s",
        prefix, operand[core_numeric ? 1U : 0U], suffix);
    if (!text || !length || length >= sizeof(compact) ||
        count <= 0 || (size_t)count >= sizeof(expected))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

/* x86 PAT cache maintenance uses a real CLFLUSH (3E prefix) until the
 * kernel alternatives pass replaces it with 66 CLFLUSHOPT. Keep the full
 * ALT metadata and the writable memory operand; no plain CLFLUSH fallback. */
bool minic_x86_64_linux_pat_clflushopt_form(
    const char *text, size_t length) {
    static const char expected[] =
        "#ALT:oldnstr661:.byte0x3e;clflush%P0"
        "662:#ALT:padding"
        ".skip-(((6651f-6641f)-(662b-661b))>0)*"
        "((6651f-6641f)-(662b-661b)),0x90"
        "663:.pushsection.altinstructions,\"a\""
        ".long661b-..long6641f-..4byte(9*32+23)"
        ".byte663b-661b.byte6651f-6641f"
        ".popsection.pushsection.altinstr_replacement,\"ax\""
        "#ALT:replacement16641:.byte0x66;clflush%P0"
        "6651:.popsection";
    char compact[512];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

static bool x86_linux_pat_clflushopt_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *address, *zero;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count != 1U || !source->outputs ||
        source->input_count != 1U || !source->inputs ||
        source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !minic_x86_64_linux_pat_clflushopt_form(
            source->template_text, source->template_length) ||
        !target_constraint_is(&source->outputs[0], "+m") ||
        !target_constraint_is(&source->inputs[0], "i")) return false;
    address = minic_c0_program_expression(
        program, source->outputs[0].expression);
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!address || address->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(address->type) ||
        !minic_type_unqualified(address->type, &plain) ||
        plain.base_kind != MINIC_TYPE_BASE_INT ||
        plain.integer_rank != MINIC_INTEGER_RANK_CHAR ||
        plain.pointer_depth != 0U ||
        !zero || !minic_type_is_integer(zero->type)) return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* GNU futex's type-17 exception handler writes -EFAULT into a specific
 * physical register. Its `.macro extable_type_reg` is not decoration: an
 * ordinary LOCK/CMPXCHG alone is wrong if a user-address access faults.
 * Recognize the WHOLE macro and both exception records in the retry-loop
 * variants, then let the x86 emitter select and encode a real register. */
unsigned int minic_x86_64_linux_futex_extable_form(
    const char *text, size_t length) {
    static const char macro[] =
        ".macroextable_type_regtype:reqreg:req"
        ".set.Lfound,0.set.Lregnr,0"
        ".irprs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15"
        ".ifc\\reg,%%\\rs"
        ".set.Lfound,.Lfound+1.long\\type+(.Lregnr<<8)"
        ".endif.set.Lregnr,.Lregnr+1.endr"
        ".set.Lregnr,0"
        ".irprs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d"
        ".ifc\\reg,%%\\rs"
        ".set.Lfound,.Lfound+1.long\\type+(.Lregnr<<8)"
        ".endif.set.Lregnr,.Lregnr+1.endr"
        ".if(.Lfound!=1)"
        ".error\"extable_type_reg:badregisterargument\""
        ".endif.endm";
    static const char *const prefix[] = {
        "1:.pushsection.smp_locks,\"a\".balign4.long671f-..popsection"
        "671:lock;cmpxchgl%3,%22:",
        "1:xchgl%0,%22:",
        "1:.pushsection.smp_locks,\"a\".balign4.long671f-..popsection"
        "671:lock;xaddl%0,%22:",
        "1:movl%2,%02:movl%0,%3orl%4,%33:.pushsection.smp_locks,\"a\""
        ".balign4.long671f-..popsection671:lock;cmpxchgl%3,%2jnz2b4:",
        "1:movl%2,%02:movl%0,%3andl%4,%33:.pushsection.smp_locks,\"a\""
        ".balign4.long671f-..popsection671:lock;cmpxchgl%3,%2jnz2b4:",
        "1:movl%2,%02:movl%0,%3xorl%4,%33:.pushsection.smp_locks,\"a\""
        ".balign4.long671f-..popsection671:lock;cmpxchgl%3,%2jnz2b4:"
    };
    static const char begin[] =
        ".pushsection\"__ex_table\",\"a\".balign4.long(";
    static const char middle[] = ")-..long(";
    static const char before_macro[] = ")-.";
    char compact[1900], expected[1900];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(prefix)/sizeof(prefix[0]); ++i) {
        const bool loop = i >= 3U;
        const char *reg = i == 0U ? "%0" : "%1";
        const char *first_to = loop ? "4b" : "2b";
        int written;

        written = snprintf(expected, sizeof(expected),
            "%s%s1b%s%s%s%s"
            "extable_type_regreg=%s,type=(17|((-14)<<16))"
            ".purgemextable_type_reg.popsection"
            "%s",
            prefix[i], begin, middle, first_to, before_macro, macro,
            reg, loop ? "__SECOND__" : "");
        if (written <= 0 || (size_t)written >= sizeof(expected))
            continue;
        if (loop) {
            char *second = strstr(expected, "__SECOND__");
            written = second == NULL ? -1 : snprintf(second,
                sizeof(expected) - (size_t)(second - expected),
                "%s3b%s4b%s%s"
                "extable_type_regreg=%%1,type=(17|((-14)<<16))"
                ".purgemextable_type_reg.popsection",
                begin, middle, before_macro, macro);
            if (written <= 0 ||
                (size_t)written >= sizeof(expected) - (size_t)(second - expected))
                continue;
        }
        if (strcmp(compact, expected) == 0)
            return (unsigned int)i + 1U;
    }
    return 0U;
}

static bool x86_linux_futex_extable_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_futex_extable_form(
        source->template_text, source->template_length) : 0U;
    const bool loop = form >= 4U;
    const size_t output_count = loop ? 4U : 3U;
    const MinicExpression *expression;
    MinicType plain;
    size_t i;
    if (!program || !source || !contract || !form ||
        !source->is_volatile || source->is_goto ||
        !source->outputs || !source->inputs ||
        source->output_count != output_count || source->input_count != 2U ||
        source->label_count || source->register_clobber_count ||
        source->has_memory_clobber != (form == 1U) ||
        source->clobber_count != (form == 1U ? 1U : 0U) ||
        !target_constraint_is(&source->outputs[0],
            form == 1U ? "+r" : loop ? "=&a" : "=r") ||
        !target_constraint_is(&source->outputs[1],
            form == 1U ? "=a" : loop ? "=&r" : "=r") ||
        !target_constraint_is(&source->outputs[2], "+m") ||
        (loop && !target_constraint_is(&source->outputs[3], "=&r")) ||
        !target_constraint_is(&source->inputs[0],
            loop || form == 1U ? "r" : "0") ||
        !target_constraint_is(&source->inputs[1], "1"))
        return false;
    for (i = 0U; i < output_count + 2U; ++i) {
        const MinicInlineAsmOperand *op = i < output_count ?
            &source->outputs[i] : &source->inputs[i - output_count];
        expression = minic_c0_program_expression(program, op->expression);
        if (!expression || (i < output_count &&
                (expression->value_category != MINIC_VALUE_LVALUE ||
                 minic_type_is_const(expression->type))) ||
            !minic_type_unqualified(expression->type, &plain) ||
            !minic_type_is_integer(plain) ||
            plain.base_kind != MINIC_TYPE_BASE_INT ||
            plain.integer_rank != MINIC_INTEGER_RANK_INT ||
            plain.pointer_depth != 0U) return false;
        contract->kinds[i] = i == 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i < output_count ?
                (i == 0U && form == 1U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    }
    contract->operand_count = output_count + 2U;
    contract->implicit_memory_effect = true;
    return true;
}

static bool x86_linux_idle_clflush_alt_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *zero, *address;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 2U ||
        !source->inputs || source->label_count || source->clobber_count ||
        source->register_clobber_count || source->has_memory_clobber ||
        !minic_x86_64_linux_idle_clflush_alt_form(
            source->template_text, source->template_length, false) ||
        !target_constraint_is(&source->inputs[0], "i") ||
        !target_constraint_is(&source->inputs[1], "a") ||
        !source->inputs[1].name || source->inputs[1].name_length != 4U ||
        memcmp(source->inputs[1].name, "addr", 4U) != 0)
        return false;
    zero = minic_c0_program_expression(
        program, source->inputs[0].expression);
    address = minic_c0_program_expression(
        program, source->inputs[1].expression);
    if (!zero || zero->kind != MINIC_EXPRESSION_INTEGER ||
        zero->value.integer_value != 0 ||
        !address || !minic_type_is_pointer(address->type))
        return false;
    contract->operand_count = 2U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Intel MDS/TAA idle mitigation clears CPU internal buffers through VERW.
 * Its selector is an actual 16-bit memory operand; the instruction has
 * hardware effects even when the input selector is a compile-time constant.
 * An arbitrary `m` GNU input is not permission for other privileged opcodes. */
bool minic_x86_64_linux_verw_clear_form(
    const char *text, size_t length) {
    static const char *const forms[] = {"verw%[ds]", "verw%0"};
    char compact[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return true;
    return false;
}

static bool x86_linux_verw_clear_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *selector;
    MinicType plain;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->output_count || source->input_count != 1U ||
        !source->inputs || source->label_count || source->clobber_count != 1U ||
        source->has_memory_clobber || source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !clobber_is(&source->register_clobbers[0], "cc") ||
        !minic_x86_64_linux_verw_clear_form(
            source->template_text, source->template_length) ||
        !target_constraint_is(&source->inputs[0], "m"))
        return false;
    selector = minic_c0_program_expression(
        program, source->inputs[0].expression);
    if (!selector || selector->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_unqualified(selector->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_short()))
        return false;
    contract->operand_count = 1U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

/* Linux debugreg.h admits DR0/DR1/DR2/DR3/DR6 and DR7 only. DR7 uses
 * a fake `m(*(u32 *)0x1000)` compiler dependency to block reordering of
 * the privileged read/write; dereferencing the fake address would be a
 * kernel fault. Transport that dependency as a memory role in Core and
 * validate it again in Target, without emitting a load/store at 0x1000. */
unsigned int minic_x86_64_linux_debug_register_form(
    const char *text, size_t length) {
    static const unsigned int names[] = {0U, 1U, 2U, 3U, 6U, 7U};
    char compact[64];
    char expected[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < 6U; ++i) {
        int count = snprintf(expected, sizeof(expected),
            "mov%%%%db%u,%%0", names[i]);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 1U;
        count = snprintf(expected, sizeof(expected),
            "mov%%0,%%%%db%u", names[i]);
        if (count > 0 && (size_t)count < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)i + 7U;
    }
    return 0U;
}

static bool x86_linux_debug_register_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    const unsigned int form = source ? minic_x86_64_linux_debug_register_form(
        source->template_text, source->template_length) : 0U;
    const bool read = form >= 1U && form <= 6U;
    const bool dr7 = form == 6U || form == 12U;
    const MinicExpression *value, *dummy;
    MinicType plain;
    if (!program || !source || !contract || !form || source->is_goto ||
        source->is_volatile != dr7 || source->output_count != (read ? 1U : 0U) ||
        source->input_count != (read ? (dr7 ? 1U : 0U) : (dr7 ? 2U : 1U)) ||
        (read && !source->outputs) ||
        (source->input_count && !source->inputs) ||
        source->label_count || source->register_clobber_count ||
        source->clobber_count || source->has_memory_clobber ||
        (read ? !target_constraint_is(&source->outputs[0], "=r") :
                !target_constraint_is(&source->inputs[0], "r")) ||
        (dr7 && !target_constraint_is(&source->inputs[read ? 0U : 1U], "m")))
        return false;
    value = minic_c0_program_expression(program, read ?
        source->outputs[0].expression : source->inputs[0].expression);
    if (!value || (read && (value->value_category != MINIC_VALUE_LVALUE ||
                           minic_type_is_const(value->type))) ||
        !minic_type_unqualified(value->type, &plain) ||
        !minic_type_equal(plain, minic_type_unsigned_long()))
        return false;
    if (dr7) {
        dummy = minic_c0_program_expression(
            program, source->inputs[read ? 0U : 1U].expression);
        if (!dummy || dummy->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(dummy->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_int()))
            return false;
    }
    contract->operand_count = dr7 ? 2U : 1U;
    contract->kinds[0] = read ?
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if (dr7)
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
    contract->implicit_memory_effect = true;
    return true;
}

bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    bool is_mulacc = false, is_muldiv = false;
    size_t i, count;
    if (!contract) return false;
    if (x86_linux_cmpxchg128_source(program, source, contract))
        return true;
    if (x86_linux_task_size_alt_source(program, source, contract))
        return true;
    if (x86_linux_irq_stack_call_source(program, source, contract))
        return true;
    if (x86_linux_task_control_source(program, source, contract))
        return true;
    if (x86_linux_iret_to_self_source(program, source, contract))
        return true;
    if (x86_linux_int3_selftest_source(program, source, contract))
        return true;
    if (x86_linux_pv_indirect_call_source(program, source, contract))
        return true;
    if (x86_linux_alt_reloc_selftest_source(program, source, contract))
        return true;
    if (x86_linux_real_mode_far_jump_source(program, source, contract))
        return true;
    if (x86_linux_mmio_read_source(program, source, contract))
        return true;
    if (x86_linux_mmio_write_source(program, source, contract))
        return true;
    if (x86_linux_rep_string_io_source(program, source, contract))
        return true;
    if (x86_linux_iomem_copy_source(program, source, contract))
        return true;
    if (x86_linux_flushcache_copy_source(program, source, contract))
        return true;
    if (x86_linux_atomic_xchg_source(program, source, contract))
        return true;
    if (x86_linux_pvclock_scale_source(program, source, contract))
        return true;
    if (x86_linux_pkru_source(program, source, contract))
        return true;
    if (x86_linux_fdiv_bug_source(program, source, contract))
        return true;
    if (x86_linux_monitor_wait_source(program, source, contract))
        return true;
    if (x86_linux_segment_read_source(program, source, contract))
        return true;
    if (x86_linux_segment_load_extable_source(program, source, contract))
        return true;
    if (x86_linux_lkgs_extable_source(program, source, contract))
        return true;
    if (x86_linux_load_unaligned_zeropad_source(program, source, contract))
        return true;
    if (x86_linux_lar_selector_source(program, source, contract))
        return true;
    if (x86_linux_random_seed_source(program, source, contract))
        return true;
    if (x86_linux_fsgsbase_source(program, source, contract))
        return true;
    if (x86_linux_kernel_nofault_goto_source(program, source, contract))
        return true;
    if (x86_linux_rep_movsb_alternative_source(program, source, contract))
        return true;
    if (x86_linux_rep_stosb_alternative_source(program, source, contract))
        return true;
    if (x86_linux_fpu_user_frame_source(program, source, contract))
        return true;
    if (x86_linux_x87_fild_prepare_source(program, source, contract))
        return true;
    if (x86_linux_fpu_kernel_restore_source(program, source, contract))
        return true;
    if (x86_linux_fxrstor_safe_source(program, source, contract))
        return true;
    if (x86_linux_xstate_fault_source(program, source, contract))
        return true;
    if (x86_linux_xrstor_alternative_source(program, source, contract))
        return true;
    if (x86_linux_xsave_triple_alternative_source(program, source, contract))
        return true;
    if (x86_linux_smap_alternative_source(program, source, contract))
        return true;
    if (x86_linux_segment_register_extable_source(program, source, contract))
        return true;
    if (x86_linux_put_user_call_source(program, source, contract))
        return true;
    if (x86_linux_get_user_call_source(program, source, contract))
        return true;
    if (x86_linux_idle_clflush_alt_source(program, source, contract))
        return true;
    if (x86_linux_pat_clflushopt_source(program, source, contract))
        return true;
    if (x86_linux_futex_extable_source(program, source, contract))
        return true;
    if (x86_linux_verw_clear_source(program, source, contract))
        return true;
    if (x86_linux_debug_register_source(program, source, contract))
        return true;
    if (x86_x87_probe_source(program, source, contract))
        return true;
    if (x86_fxsave_source(program, source, contract))
        return true;
    if (x86_xsetbv_source(program, source, contract))
        return true;
    if (x86_linux_alternative_wrmsr_source(program, source, contract))
        return true;
    if (x86_linux_control_register_source(program, source, contract))
        return true;
    if (x86_linux_tlb_cache_source(program, source, contract))
        return true;
    if (x86_linux_three_call_alternative_source(program, source, contract))
        return true;
    if (x86_linux_boot_segments_source(program, source, contract))
        return true;
    if (x86_linux_lidt_source(program, source, contract))
        return true;
    if (x86_linux_gs_testcc_source(program, source, contract))
        return true;
    if (x86_linux_testcc_source(program, source, contract))
        return true;
    if (x86_prefetchw_alt_source(program, source, contract))
        return true;
    if (x86_apic_amd_alt_source(program, source, contract))
        return true;
    if (x86_gs_tied_alu_source(program, source, contract))
        return true;
    if (x86_linux_zero_barrier_source(source, contract))
        return true;
    if (x86_linux_ip_here_source(program, source, contract))
        return true;
    if (x86_add32_carry_source(program, source, contract))
        return true;
    if (x86_linux_tcpudp_nofold_source(program, source, contract))
        return true;
    if (x86_percpu_gs_source(program, source, contract))
        return true;
    if (x86_ip_fast_csum_source(program, source, contract))
        return true;
    if (x86_checksum_source(program, source, contract))
        return true;
    if (x86_linux_msr_source(program, source, contract))
        return true;
    if (x86_hweight_source(program, source, contract))
        return true;
    if (x86_counter_source(program, source, contract))
        return true;
    if (x86_port_io_source(program, source, contract))
        return true;
    if (x86_linux_smp_xadd_source(program, source, contract))
        return true;
    if (x86_linux_local_xadd_source(program, source, contract))
        return true;
    if (x86_linux_percpu_xadd_source(program, source, contract))
        return true;
    if (x86_linux_gs_cmpxchg_source(program, source, contract))
        return true;
    if (x86_linux_rep_stos_source(program, source, contract))
        return true;
    if (x86_linux_lowlevel_scalar_source(program, source, contract))
        return true;
    if (x86_linux_boot_flags_source(program, source, contract))
        return true;
    if (x86_linux_delay_mul_source(program, source, contract))
        return true;
    if (x86_linux_delay_wait_source(program, source, contract))
        return true;
    if (x86_linux_identity_load_source(program, source, contract))
        return true;
    if (x86_linux_ipv6_checksum_source(program, source, contract))
        return true;
    if (x86_linux_lock_bitcc_source(program, source, contract))
        return true;
    if (x86_linux_boot_string_source(program, source, contract))
        return true;
    if (x86_linux_vdso_syscall_source(program, source, contract))
        return true;
    if (x86_linux_vdso_cpunode_source(program, source, contract))
        return true;
    if (x86_linux_csum_partial_source(program, source, contract))
        return true;
    if (x86_linux_atomic_rmw_source(program, source, contract))
        return true;
    if (x86_linux_raw_cmpxchg_source(program, source, contract))
        return true;
    if (x86_trycas_source(program, source, contract))
        return true;
    if (x86_linux_bitops_source(program, source, contract))
        return true;
    if (x86_linux_atomic_cc_source(program, source)) {
        const MinicX86AtomicCCDesc *d = minic_x86_64_atomic_cc_description(
            minic_x86_64_atomic_cc_form(
                source->template_text, source->template_length));
        contract->operand_count = d && d->has_value ? 3U : 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (contract->operand_count == 3U)
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (x86_nospec_source(program, source)) {
        contract->operand_count = 3U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (x86_percpu_source(program, source)) {
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (!qualified(program, source, &is_mulacc, &is_muldiv)) return false;
    (void)is_muldiv; /* all four mul/div operand roles are scalar/register */
    count = source->output_count + source->input_count;
    if (count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) return false;
    contract->operand_count = count;
    for (i = 0U; i < count; ++i)
        contract->kinds[i] = i < source->output_count
            ? (minic_x86_64_asm_bit_scan_form(
                   source->template_text, source->template_length) ==
               MINIC_X86_BIT_SCAN_BSR64_READWRITE ?
                   MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                   MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT)
            : is_mulacc && i == 7U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
