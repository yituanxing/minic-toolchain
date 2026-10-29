#include "target/loongarch64/simd_asm.h"
#include "target/loongarch64/frame_emit.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Original Linux 6.6.143 lib/raid6/loongarch_simd.c,
 * lib/raid6/recov_loongarch_simd.c, arch/loongarch/lib/xor_simd.c.
 * These kernels keep vector accumulators in the architecturally fixed
 * $vr/$xr registers across *separate* volatile GNU asm statements.
 * Their non-vector C operations use normal GPRs. We emit every vector
 * instruction with its exact ISA effect, and load memory operand pointers
 * from typed Core value slots. Unknown mnemonics, directives, constraints,
 * register classes, offsets and extra instructions remain rejected. */
typedef enum LoongSimdOp {
    LOONG_SIMD_LOAD,
    LOONG_SIMD_STORE,
    LOONG_SIMD_XOR,
    LOONG_SIMD_AND_IMMEDIATE,
    LOONG_SIMD_OR_IMMEDIATE,
    LOONG_SIMD_SLTI,
    LOONG_SIMD_SLLI,
    LOONG_SIMD_SRLI,
    LOONG_SIMD_SHUFFLE,
    LOONG_SIMD_REPLICATE
} LoongSimdOp;

typedef struct LoongSimdInstruction {
    LoongSimdOp op;
    bool lasx;
    unsigned int registers[4];
    size_t operand_index;
    int memory_offset;
    char machine_line[192];
} LoongSimdInstruction;

typedef struct LoongSimdProgram {
    LoongSimdInstruction instructions[64];
    size_t count;
    size_t operand_count;
    bool memory_read[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool memory_written[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT];
    bool uses_lsx;
    bool uses_lasx;
} LoongSimdProgram;

typedef struct LoongSimdTemplate {
    const char *name;
    LoongSimdOp op;
    unsigned int operands;
    bool lasx;
    int required_imm;
} LoongSimdTemplate;

static const LoongSimdTemplate loong_simd_templates[] = {
    {"vld", LOONG_SIMD_LOAD, 2U, false, -1},
    {"vst", LOONG_SIMD_STORE, 2U, false, -1},
    {"vxor.v", LOONG_SIMD_XOR, 3U, false, -1},
    {"vandi.b", LOONG_SIMD_AND_IMMEDIATE, 3U, false, -1},
    {"vori.b", LOONG_SIMD_OR_IMMEDIATE, 3U, false, 0},
    {"vslti.b", LOONG_SIMD_SLTI, 3U, false, 0},
    {"vslli.b", LOONG_SIMD_SLLI, 3U, false, 1},
    {"vsrli.b", LOONG_SIMD_SRLI, 3U, false, 4},
    {"vshuf.b", LOONG_SIMD_SHUFFLE, 4U, false, -1},
    {"xvld", LOONG_SIMD_LOAD, 2U, true, -1},
    {"xvst", LOONG_SIMD_STORE, 2U, true, -1},
    {"xvxor.v", LOONG_SIMD_XOR, 3U, true, -1},
    {"xvandi.b", LOONG_SIMD_AND_IMMEDIATE, 3U, true, -1},
    {"xvori.b", LOONG_SIMD_OR_IMMEDIATE, 3U, true, 0},
    {"xvslti.b", LOONG_SIMD_SLTI, 3U, true, 0},
    {"xvslli.b", LOONG_SIMD_SLLI, 3U, true, 1},
    {"xvsrli.b", LOONG_SIMD_SRLI, 3U, true, 4},
    {"xvshuf.b", LOONG_SIMD_SHUFFLE, 4U, true, -1},
    {"xvreplve0.q", LOONG_SIMD_REPLICATE, 2U, true, -1}
};

static bool loong_simd_register(const char *text, bool lasx,
                                unsigned int *reg) {
    const char *p;
    unsigned int value = 0U;
    if (!text || !reg) return false;
    if (lasx) {
        if (strncmp(text, "$xr", 3U) != 0) return false;
    } else if (strncmp(text, "$vr", 3U) != 0) return false;
    p = text + 3U;
    if (*p < '0' || *p > '9') return false;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9' || value > 3U) return false;
        value = value * 10U + (unsigned int)(*p - '0');
    }
    if (value >= 32U) return false;
    *reg = value;
    return true;
}

static bool loong_simd_immediate(const char *text, int *value) {
    const char *p = text;
    int base = 10, out = 0;
    if (!p || !*p || !value) return false;
    if (p[0] == '0' && p[1] == 'x') {
        p += 2U;
        base = 16;
        if (!*p) return false;
    }
    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (base == 16 && *p >= 'a' && *p <= 'f')
            digit = *p - 'a' + 10;
        else return false;
        if (digit >= base || out > (255 - digit) / base) return false;
        out = out * base + digit;
        ++p;
    }
    *value = out;
    return true;
}

static bool loong_simd_placeholder(const char *text, size_t *index,
                                    bool *named) {
    size_t v = 0U;
    const char *p;
    if (!text || text[0] != '%' || !index || !named) return false;
    if (text[1] == '[') {
        if (text[2] != 'v' || text[3] < '1' || text[3] > '5' ||
            text[4] != ']' || text[5] != '\0') return false;
        *index = (size_t)(text[3] - '1');
        *named = true;
        return true;
    }
    for (p = text + 1U; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10U + (size_t)(*p - '0');
        if (v >= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) return false;
    }
    if (p == text + 1U) return false;
    *index = v;
    *named = false;
    return true;
}

/* Parse one native machine statement, with exact comma/operand grammar.
 * A destination `vld` source and `vst` destination are typed pointer
 * placeholders; all other opcodes use ONLY literal vector registers. */
static bool loong_simd_line(char *line, LoongSimdInstruction *ins,
                            size_t *needed_operand, bool *named_operand) {
    char *cursor = line, *opcode, *argv[4];
    const LoongSimdTemplate *form = NULL;
    size_t i, argc = 0U;
    int imm;
    if (!line || !ins || !needed_operand || !named_operand) return false;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!*cursor) return false;
    opcode = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (!*cursor) return false;
    *cursor++ = '\0';
    for (i = 0U; i < sizeof(loong_simd_templates)/sizeof(loong_simd_templates[0]); ++i)
        if (strcmp(opcode, loong_simd_templates[i].name) == 0) {
            form = &loong_simd_templates[i];
            break;
        }
    if (!form) return false;
    while (*cursor) {
        char *first;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor) break;
        if (argc >= 4U || *cursor == ',' || *cursor == ';') return false;
        first = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != ',')
            ++cursor;
        if (cursor == first) return false;
        if (*cursor) {
            const char sep = *cursor;
            *cursor++ = '\0';
            if (sep != ',') {
                while (*cursor == ' ' || *cursor == '\t') ++cursor;
                if (*cursor != ',') return false;
                ++cursor;
            }
        }
        argv[argc++] = first;
    }
    if (argc != form->operands &&
        !(form->op == LOONG_SIMD_LOAD && argc == 3U) &&
        !(form->op == LOONG_SIMD_STORE && argc == 3U)) return false;
    ins->op = form->op;
    ins->lasx = form->lasx;
    if (!loong_simd_register(argv[0], form->lasx, &ins->registers[0]))
        return false;
    *needed_operand = SIZE_MAX;
    *named_operand = false;
    if (form->op == LOONG_SIMD_LOAD || form->op == LOONG_SIMD_STORE) {
        if (!loong_simd_placeholder(argv[1], needed_operand, named_operand))
            return false;
        ins->operand_index = *needed_operand;
        ins->memory_offset = 0;
        if (argc == 3U &&
            (!loong_simd_immediate(argv[2], &ins->memory_offset) ||
             (ins->memory_offset != 0 && ins->memory_offset != 16 &&
              ins->memory_offset != 32 && ins->memory_offset != 48)))
            return false;
        return true;
    }
    for (i = 1U; i < argc; ++i) {
        if ((form->op == LOONG_SIMD_AND_IMMEDIATE ||
             form->op == LOONG_SIMD_OR_IMMEDIATE ||
             form->op == LOONG_SIMD_SLTI ||
             form->op == LOONG_SIMD_SLLI ||
             form->op == LOONG_SIMD_SRLI) && i == argc - 1U) {
            if (!loong_simd_immediate(argv[i], &imm) ||
                (form->op == LOONG_SIMD_AND_IMMEDIATE &&
                 imm != 0x0f && imm != 0x1d) ||
                (form->op != LOONG_SIMD_AND_IMMEDIATE &&
                 imm != form->required_imm)) return false;
        } else if (!loong_simd_register(
                       argv[i], form->lasx, &ins->registers[i]))
            return false;
    }
    return true;
}

static bool loong_simd_parse(const char *text, size_t length,
                              LoongSimdProgram *program) {
    size_t pos = 0U;
    if (!text || !length || !program || length > 8192U) return false;
    memset(program, 0, sizeof(*program));
    while (pos < length) {
        char line[192], *begin, *end;
        size_t first = pos, size, index;
        bool named;
        LoongSimdInstruction *ins;
        while (pos < length && text[pos] != '\n') {
            if (text[pos] == '\0' || text[pos] == '\r' ||
                text[pos] == ';' || text[pos] == '#') return false;
            ++pos;
        }
        size = pos - first;
        if (pos < length && text[pos] == '\n') ++pos;
        if (size >= sizeof(line)) return false;
        memcpy(line, text + first, size);
        line[size] = '\0';
        begin = line;
        while (*begin == ' ' || *begin == '\t') ++begin;
        end = begin + strlen(begin);
        while (end > begin && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        if (!*begin) continue;
        if (program->count >= sizeof(program->instructions)/
                                    sizeof(program->instructions[0])) return false;
        ins = &program->instructions[program->count];
        if (strlen(begin) >= sizeof(ins->machine_line)) return false;
        strcpy(ins->machine_line, begin);
        if (!loong_simd_line(begin, ins, &index, &named)) return false;
        program->uses_lasx |= ins->lasx;
        program->uses_lsx |= !ins->lasx;
        if (index != SIZE_MAX) {
            if (index >= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT)
                return false;
            /* Linux RAID6 is numeric `m/+m`; Linux XOR is named `r`.
             * Both families keep a single placeholder convention. */
            if (named != (strstr(ins->machine_line, "%[") != NULL))
                return false;
            if (ins->op == LOONG_SIMD_LOAD)
                program->memory_read[index] = true;
            else program->memory_written[index] = true;
            if (index + 1U > program->operand_count)
                program->operand_count = index + 1U;
        }
        ++program->count;
    }
    if (!program->count || (program->uses_lasx && program->uses_lsx))
        return false;
    return true;
}

static bool loong_simd_constraint(const MinicInlineAsmOperand *op,
                                  const char *expected) {
    const size_t n = strlen(expected);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, expected, n) == 0;
}

static bool loong_simd_source_operand(
    const MinicC0Program *program, const MinicInlineAsmOperand *op,
    bool output, bool named, size_t index,
    MinicCoreStructuredInlineAsmOperandKind *kind) {
    const MinicExpression *expr;
    char expected_name[4];
    if (!program || !op || !kind) return false;
    if (named) {
        if (output || index >= 5U ||
            snprintf(expected_name, sizeof(expected_name), "v%zu", index + 1U) < 0 ||
            !op->name || op->name_length != 2U ||
            memcmp(op->name, expected_name, 2U) != 0)
            return false;
    } else if (op->name_length) return false;
    expr = minic_c0_program_expression(program, op->expression);
    if (!expr) return false;
    if (!output && named && loong_simd_constraint(op, "r") &&
        minic_type_is_pointer(expr->type)) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        return true;
    }
    if (expr->value_category != MINIC_VALUE_LVALUE ||
        (output && minic_type_is_const(expr->type))) return false;
    if (output && loong_simd_constraint(op, "+m") &&
        op->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        return true;
    }
    if (output && loong_simd_constraint(op, "=m") &&
        op->access == MINIC_INLINE_ASM_OPERAND_WRITE_ONLY) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        return true;
    }
    if (!output && !named && loong_simd_constraint(op, "m") &&
        op->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
        *kind = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT;
        return true;
    }
    return false;
}

bool minic_loongarch64_simd_qualify_source(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    LoongSimdProgram parsed;
    size_t i;
    bool named, uses_load = false, uses_store = false;
    if (!program || !source || !contract || !source->is_volatile ||
        source->is_goto || source->label_count ||
        source->register_clobber_count ||
        !loong_simd_parse(source->template_text, source->template_length,
                          &parsed) || parsed.operand_count == 0U ||
        parsed.operand_count != source->output_count + source->input_count ||
        parsed.operand_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        (source->output_count && !source->outputs) ||
        (source->input_count && !source->inputs)) return false;
    named = strstr(source->template_text, "%[") != NULL;
    if (named) {
        if (source->output_count || source->input_count < 2U ||
            source->input_count > 5U || !source->has_memory_clobber ||
            source->clobber_count != 1U) return false;
    } else if (source->has_memory_clobber || source->clobber_count)
        return false;
    for (i = 0U; i < parsed.operand_count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *op = output
            ? &source->outputs[i] : &source->inputs[i-source->output_count];
        MinicCoreStructuredInlineAsmOperandKind role;
        const bool read = parsed.memory_read[i];
        const bool written = parsed.memory_written[i];
        if ((!read && !written) ||
            !loong_simd_source_operand(program, op, output, named,
                                       i, &role)) return false;
        if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
            if (!named) return false;
        } else if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
            if (!read || written) return false;
        } else if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) {
            if (!written || read) return false;
        } else if (role == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            if (!read || !written) return false;
        } else return false;
        uses_load |= read;
        uses_store |= written;
        contract->kinds[i] = role;
    }
    /* A vector load/store must correspond to a *real* typed memory effect.
     * Named pointer forms carry a GNU `memory` clobber; numeric forms bind
     * exact read/write `m` C lvalues and preserve their volatile ordering. */
    if (!uses_load && !uses_store) return false;
    contract->operand_count = parsed.operand_count;
    contract->implicit_memory_effect = named;
    return true;
}

bool minic_loongarch64_simd_qualify_opaque(const MinicCoreInlineAsm *source) {
    LoongSimdProgram parsed;
    if (!source || !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->register_clobber_count ||
        !loong_simd_parse(source->template_text, source->template_length,
                          &parsed) || parsed.operand_count != 0U) return false;
    return true;
}

bool minic_loongarch64_simd_emit_opaque(
    FILE *out, const MinicCoreInlineAsm *source) {
    LoongSimdProgram parsed;
    size_t i;
    if (!out || !minic_loongarch64_simd_qualify_opaque(source) ||
        !loong_simd_parse(source->template_text, source->template_length,
                          &parsed)) return false;
    for (i = 0U; i < parsed.count; ++i)
        if (fprintf(out, "  %s\n", parsed.instructions[i].machine_line) < 0)
            return false;
    return true;
}

/* Core canonicalizes GNU named operands to positional operands.  A trusted
 * source id identifies the one original GNU statement; the backend must
 * still prove that its syntax, operand roles and machine effects match the
 * lowered Core program.  Never accept a source id as the proof itself. */
static bool loong_simd_same_machine_effects(
    const LoongSimdProgram *original, const LoongSimdProgram *machine) {
    size_t i, j;
    if (!original || !machine || original->count != machine->count ||
        original->operand_count != machine->operand_count) return false;
    for (i = 0U; i < original->count; ++i) {
        const LoongSimdInstruction *a = &original->instructions[i];
        const LoongSimdInstruction *b = &machine->instructions[i];
        if (a->op != b->op || a->lasx != b->lasx ||
            a->operand_index != b->operand_index ||
            a->memory_offset != b->memory_offset) return false;
        for (j = 0U; j < 4U; ++j)
            if (a->registers[j] != b->registers[j]) return false;
    }
    return true;
}

bool minic_loongarch64_simd_emit_structured(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *operands;
    const MinicInlineAsm *original;
    MinicTargetInlineAsmContract contract = {0};
    LoongSimdProgram parsed, source_parsed;
    size_t i;
    bool named, expected_memory_effect;
    if (!out || !ctx || !(fn = ctx->fn) || !ins || !source ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(ins->type) || !source->is_volatile ||
        source->is_goto || source->register_clobber_count ||
        !ctx->program ||
        source->source_inline_asm_id == SIZE_MAX ||
        source->source_inline_asm_id >= ctx->program->inline_asm_count ||
        !loong_simd_parse(source->template_text, source->template_length,
                          &parsed) || parsed.operand_count == 0U ||
        parsed.operand_count != ins->value.structured_inline_asm.operand_count ||
        !(operands = ins->value.structured_inline_asm.operands))
        return false;
    original = minic_c0_program_inline_asm(
        ctx->program, source->source_inline_asm_id);
    if (!original ||
        !minic_loongarch64_simd_qualify_source(
            ctx->program, original, &contract) ||
        !loong_simd_parse(original->template_text,
                          original->template_length, &source_parsed) ||
        contract.operand_count != parsed.operand_count ||
        !loong_simd_same_machine_effects(&source_parsed, &parsed))
        return false;
    named = strstr(original->template_text, "%[") != NULL;
    expected_memory_effect =
        original->has_memory_clobber || contract.implicit_memory_effect;
    for (i = 0U; i < parsed.operand_count; ++i) {
        if (contract.kinds[i] != operands[i].kind) return false;
        if (contract.kinds[i] ==
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE)
            expected_memory_effect = true;
    }
    if (source->has_memory_clobber != expected_memory_effect)
        return false;
    /* Core marks read/write +m as an implicit memory effect, even though
     * the original GNU source had no literal "memory" clobber. Named r
     * pointer forms carry that explicit source clobber. */
    if (named && !source->has_memory_clobber) return false;
    if (named && (parsed.operand_count < 2U || parsed.operand_count > 5U))
        return false;
    for (i = 0U; i < parsed.operand_count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *operand = &operands[i];
        const bool read = parsed.memory_read[i];
        const bool written = parsed.memory_written[i];
        const MinicCoreStructuredInlineAsmOperandKind kind = operand->kind;
        if (operand->operand_index != i || operand->early_clobber ||
            operand->has_fixed_register_binding ||
            operand->value >= fn->value_count ||
            !minic_type_is_pointer(fn->values[operand->value].type) ||
            (!read && !written) || contract.kinds[i] != kind) return false;
        if (named) {
            if (kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
                return false;
        } else if (kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) {
            if (!read || written) return false;
        } else if (kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) {
            if (read || !written) return false;
        } else if (kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            if (!read || !written) return false;
        } else return false;
    }
    /* All instructions are validated first; a new opcode cannot leak into
     * partially generated assembler after a successful source qualifier. */
    for (i = 0U; i < parsed.count; ++i) {
        const LoongSimdInstruction *step = &parsed.instructions[i];
        if (step->op == LOONG_SIMD_LOAD || step->op == LOONG_SIMD_STORE) {
            const MinicCoreValueId value =
                operands[step->operand_index].value;
            const char *op = step->op == LOONG_SIMD_LOAD
                ? (step->lasx ? "xvld" : "vld")
                : (step->lasx ? "xvst" : "vst");
            const char *reg = step->lasx ? "$xr" : "$vr";
            if (!minic_loongarch64_emit_frame_mem(
                    out, "ld.d", "$t0", (size_t)value * 8U,
                    ctx->dynamic_frame) ||
                fprintf(out, "  %s %s%u, $t0, %d\n", op, reg,
                        step->registers[0], step->memory_offset) < 0)
                return false;
        } else if (fprintf(out, "  %s\n", step->machine_line) < 0)
            return false;
    }
    return true;
}
