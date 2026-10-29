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
    static const char *const exact[] = {"lidt %0", "lgdt %0"};
    size_t i;
    if (!text) return 0U;
    for (i = 0U; i < sizeof(exact) / sizeof(exact[0]); ++i)
        if (length == strlen(exact[i]) &&
            memcmp(text, exact[i], length) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
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
        src->output_count != 0U || src->input_count != 1U || !src->inputs ||
        src->label_count != 0U || src->clobber_count != 0U ||
        src->register_clobber_count != 0U || src->has_memory_clobber ||
        !target_constraint_is(&src->inputs[0], "m")) return false;
    descriptor = minic_c0_program_expression(
        program, src->inputs[0].expression);
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
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
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

/* Linux interrupt flag and halted-CPU state transitions. These four exact
 * zero-operand GNU templates are privileged machine instructions; their
 * compiler memory sequencing is essential, and the Target must not
 * reinterpret them as ordinary optimizer barriers or omit the instruction. */
unsigned int minic_x86_64_linux_privileged_zero_form(
    const char *text, size_t length) {
    static const char *const forms[] = {"cli", "sti", "sti; hlt", "hlt"};
    size_t i;
    if (!text || length == 0U || length > 32U) return 0U;
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (length == strlen(forms[i]) &&
            memcmp(text, forms[i], length) == 0)
            return (unsigned int)(i + 1U);
    return 0U;
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

bool minic_x86_64_qualify_inline_asm_goto(
    const MinicC0Program *program, const MinicInlineAsm *source) {
    static const char *const names[] = {"feature", "bitnum", "cap_byte"};
    static const char *const labels[] = {"t_yes", "t_no"};
    const MinicExpression *expression;
    size_t i;
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
        "mov%0,%%cr2", "mov%0,%%cr3"
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
    const MinicExpression *value, *fake_memory;
    MinicType plain;
    if (!program || !source || !contract || !form || !source->is_volatile ||
        source->is_goto || source->label_count != 0U ||
        source->register_clobber_count != 0U ||
        source->output_count != (read_cr ? 1U : 0U) ||
        source->input_count != 1U || !source->inputs ||
        (read_cr && !source->outputs) ||
        source->clobber_count != (read_cr ? 0U : 1U) ||
        source->has_memory_clobber == read_cr) return false;
    if (read_cr) {
        if (!target_constraint_is(&source->outputs[0], "=r") ||
            !target_constraint_is(&source->inputs[0], "m")) return false;
        value = minic_c0_program_expression(
            program, source->outputs[0].expression);
        fake_memory = minic_c0_program_expression(
            program, source->inputs[0].expression);
        if (!value || value->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(value->type) ||
            !minic_type_unqualified(value->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()) ||
            !fake_memory || fake_memory->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_unqualified(fake_memory->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_int())) return false;
        contract->operand_count = 2U;
        contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
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
           (!(op==0U && i==1U && width==8U && minic_type_is_pointer(ty)) &&
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
    size_t i, n = 0U, id, variant;
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
        for (variant = 0U; variant < 2U; ++variant) {
            const char *mem = variant ? "%0" : "%[var]";
            const char *value = variant ? "%2" : "%[val]";
            const int written = d->has_value ?
                snprintf(expected, sizeof(expected),
                    "%s%s%s,%s/*outputconditioncode%s*/",
                    prefix, d->opcode, value, mem, x86_atomic_cc_forms[id].flag) :
                snprintf(expected, sizeof(expected),
                    "%s%s%s/*outputconditioncode%s*/",
                    prefix, d->opcode, mem, x86_atomic_cc_forms[id].flag);
            if (written > 0 && (size_t)written < sizeof(expected) &&
                strcmp(compact, expected) == 0)
                return (MinicX86AtomicCC)(id + 1U);
        }
    }
    return MINIC_X86_ATOMIC_CC_NONE;
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
            !minic_type_equal(vt, mt)) return false;
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

bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    bool is_mulacc = false, is_muldiv = false;
    size_t i, count;
    if (!contract) return false;
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
    if (x86_gs_tied_alu_source(program, source, contract))
        return true;
    if (x86_linux_zero_barrier_source(source, contract))
        return true;
    if (x86_linux_ip_here_source(program, source, contract))
        return true;
    if (x86_add32_carry_source(program, source, contract))
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
