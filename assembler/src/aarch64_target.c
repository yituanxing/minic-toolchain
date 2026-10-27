#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * First non-RISC-V MiniAS vertical slice.
 *
 * The common assembler owns sections, labels, fixed-size statement layout,
 * symbols and ELF writing. AArch64 owns instructions, immediate syntax,
 * executable NOP alignment and relocation types. Unsupported encodings fail
 * closed rather than pretending to support the entire AArch64 ISA.
 */

enum {
    AARCH64_R_ABS64 = 257U,
    AARCH64_R_ABS32 = 258U,
    AARCH64_R_JUMP26 = 282U,
    AARCH64_R_CALL26 = 283U
};

static bool parse_register(const char *text, unsigned *reg, bool *is_64) {
    char *end;
    unsigned long value;
    if ((text[0] != 'x' && text[0] != 'w') ||
        !isdigit((unsigned char)text[1])) return false;
    errno = 0;
    value = strtoul(text + 1, &end, 10);
    if (errno != 0 || *end != '\0' || value > 30U) return false;
    *reg = (unsigned)value;
    *is_64 = text[0] == 'x';
    return true;
}

static bool parse_imm16(const char *text, unsigned *result) {
    char *end;
    unsigned long value;
    if (*text != '#') return false;
    errno = 0;
    value = strtoul(text + 1, &end, 0);
    if (errno != 0 || end == text + 1 || *end != '\0' || value > 65535U)
        return false;
    *result = (unsigned)value;
    return true;
}

static bool split_two(const char *args, char *first, size_t cap1,
                      char *second, size_t cap2) {
    const char *comma = strchr(args, ',');
    size_t length;
    if (comma == NULL || strchr(comma + 1, ',') != NULL) return false;
    length = (size_t)(comma - args);
    while (length != 0U && isspace((unsigned char)args[length - 1U]))
        --length;
    if (length == 0U || length >= cap1) return false;
    memcpy(first, args, length);
    first[length] = '\0';
    args = comma + 1;
    while (isspace((unsigned char)*args)) ++args;
    length = strlen(args);
    while (length != 0U && isspace((unsigned char)args[length - 1U]))
        --length;
    if (length == 0U || length >= cap2) return false;
    memcpy(second, args, length);
    second[length] = '\0';
    return true;
}

/* AArch64 is fixed 4B. Measurement consumes the parsed MCInst just as
 * the RISC-V variable-length backend does; no duplicate mnemonic allowlist
 * or operand parsing is necessary before the instruction fragment exists. */
static bool aarch64_measure_mcinst(MiniAs *as, const MiniAsStmt *stmt,
                                   uint32_t *size) {
    if (stmt->mcinst == NULL) {
        minias_set_error(as, "missing-aarch64-mcinst:line=%zu", stmt->line);
        return false;
    }
    *size = 4U;
    return true;
}

static bool append_word(MiniAs *as, int section, uint32_t word) {
    unsigned char bytes[4];
    unsigned i;
    for (i = 0U; i != 4U; ++i)
        bytes[i] = (unsigned char)(word >> (i * 8U));
    return minias_section_append(as, section, bytes, sizeof(bytes));
}

/* First fully typed backend: parse GNU A64 operand syntax once, when the
 * instruction fragment is recorded. Encoding consumes only MCInst fields,
 * never re-splits source operand strings or reparses reloc expressions. */
enum {
    A64_MC_NOP = 1,
    A64_MC_RET,
    A64_MC_MOVZ,
    A64_MC_SVC,
    A64_MC_B,
    A64_MC_BL
};

static bool parse_mcinst(MiniAs *as, MiniAsStmt *stmt) {
    MiniAsMCInst *inst = calloc(1U, sizeof(*inst) +
                                2U * sizeof(MiniAsMCOperand));
    unsigned reg, imm;
    bool reg64;
    char first[80], second[80];
    const char *op = stmt->op;
    const char *args = stmt->args;

    if (inst == NULL) {
        minias_set_error(as, "out-of-memory:aarch64-mcinst");
        return false;
    }
    if (strcmp(op, "nop") == 0 && *args == '\0') {
        inst->opcode = A64_MC_NOP;
    } else if (strcmp(op, "ret") == 0 && *args == '\0') {
        inst->opcode = A64_MC_RET;
    } else if (strcmp(op, "ret") == 0 &&
               parse_register(args, &reg, &reg64) && reg64) {
        inst->opcode = A64_MC_RET;
        inst->count = 1U;
        inst->operands[0].kind = MINIAS_MC_REGISTER;
        inst->operands[0].reg = reg;
        inst->operands[0].register_64 = true;
    } else if (strcmp(op, "mov") == 0 &&
               split_two(args, first, sizeof(first), second, sizeof(second)) &&
               parse_register(first, &reg, &reg64) &&
               parse_imm16(second, &imm)) {
        inst->opcode = A64_MC_MOVZ;
        inst->count = 2U;
        inst->operands[0].kind = MINIAS_MC_REGISTER;
        inst->operands[0].reg = reg;
        inst->operands[0].register_64 = reg64;
        inst->operands[1].kind = MINIAS_MC_IMMEDIATE;
        inst->operands[1].immediate = imm;
    } else if (strcmp(op, "svc") == 0 && parse_imm16(args, &imm)) {
        inst->opcode = A64_MC_SVC;
        inst->count = 1U;
        inst->operands[0].kind = MINIAS_MC_IMMEDIATE;
        inst->operands[0].immediate = imm;
    } else if (strcmp(op, "b") == 0 || strcmp(op, "bl") == 0) {
        MiniAsSymbolExpr expr;
        if (!minias_parse_symbol_addend(args, &expr)) {
            minias_set_error(as, "bad-aarch64-branch-target:%s:line=%zu",
                             args, stmt->line);
            free(inst);
            return false;
        }
        if ((expr.addend & 3) != 0) {
            minias_set_error(as, "bad-aarch64-branch-addend:%s:line=%zu",
                             args, stmt->line);
            free(inst);
            return false;
        }
        inst->operands[0].symbol = malloc(sizeof(expr));
        if (inst->operands[0].symbol == NULL) {
            minias_set_error(as, "out-of-memory:aarch64-symbol-mcinst");
            free(inst);
            return false;
        }
        *inst->operands[0].symbol = expr;
        inst->opcode = strcmp(op, "bl") == 0 ? A64_MC_BL : A64_MC_B;
        inst->count = 1U;
        inst->operands[0].kind = MINIAS_MC_SYMBOL;
    } else {
        minias_set_error(as, "bad-aarch64-operands:%s:%s:line=%zu",
                         op, args, stmt->line);
        free(inst);
        return false;
    }
    stmt->mcinst = inst;
    return true;
}

static bool encode(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *inst = stmt->mcinst;
    uint32_t code;
    MiniAsFixupKind fixup = MINIAS_FIXUP_NONE;
    const MiniAsSymbolExpr *expr = NULL;
    if (inst == NULL) {
        minias_set_error(as, "missing-aarch64-mcinst:line=%zu", stmt->line);
        return false;
    }
    switch (inst->opcode) {
        case A64_MC_NOP:
            code = UINT32_C(0xd503201f);
            break;
        case A64_MC_RET:
            code = inst->count == 0U ? UINT32_C(0xd65f03c0) :
                UINT32_C(0xd65f0000) | (inst->operands[0].reg << 5U);
            break;
        case A64_MC_MOVZ:
            code = (inst->operands[0].register_64 ?
                    UINT32_C(0xd2800000) : UINT32_C(0x52800000)) |
                ((uint32_t)inst->operands[1].immediate << 5U) |
                inst->operands[0].reg;
            break;
        case A64_MC_SVC:
            code = UINT32_C(0xd4000001) |
                ((uint32_t)inst->operands[0].immediate << 5U);
            break;
        case A64_MC_B:
        case A64_MC_BL: {
            const MiniAsSymbol *target;
            int64_t distance;
            expr = inst->operands[0].symbol;
            code = inst->opcode == A64_MC_BL ?
                UINT32_C(0x94000000) : UINT32_C(0x14000000);
            fixup = inst->opcode == A64_MC_BL ?
                MINIAS_FIXUP_A64_CALL26 : MINIAS_FIXUP_A64_JUMP26;
            target = minias_get_symbol(as, expr->name, false);
            if (target == NULL || !target->defined ||
                target->section != stmt->section) break;
            if (target->value > INT64_MAX || stmt->offset > INT64_MAX) {
                minias_set_error(as, "aarch64-branch-address-overflow");
                return false;
            }
            distance = (int64_t)target->value - (int64_t)stmt->offset;
            if ((expr->addend > 0 && distance > INT64_MAX - expr->addend) ||
                (expr->addend < 0 && distance < INT64_MIN - expr->addend)) {
                minias_set_error(as, "aarch64-branch-addend-overflow");
                return false;
            }
            distance += expr->addend;
            if ((distance & 3) != 0 ||
                distance < -INT64_C(134217728) ||
                distance > INT64_C(134217724)) {
                minias_set_error(as, "aarch64-branch-out-of-range:%s:line=%zu",
                                 stmt->args, stmt->line);
                return false;
            }
            code |= (uint32_t)(distance / 4) & UINT32_C(0x03ffffff);
            fixup = MINIAS_FIXUP_NONE;
            break;
        }
        default:
            minias_set_error(as, "unknown-aarch64-mc-opcode:%u:line=%zu",
                             inst->opcode, stmt->line);
            return false;
    }
    if (!append_word(as, stmt->section, code)) return false;
    return fixup == MINIAS_FIXUP_NONE ||
           minias_add_fixup(as, stmt->section, stmt->offset, fixup, 4U,
                            expr->name, expr->addend);
}

static bool directive(MiniAs *as, const char *op, const char *args,
                      size_t line, bool *handled) {
    *handled = false;
    /* GCC emits .arch armv8-a before its first A64 instruction. Verify the
     * exact declared ISA here; never silently accept a richer architecture. */
    if (strcmp(op, ".arch") == 0) {
        *handled = true;
        if (strcmp(args, "armv8-a") != 0) {
            minias_set_error(as, "unsupported-aarch64-architecture:%s:line=%zu",
                             args, line);
            return false;
        }
        return true;
    }
    if (strcmp(op, ".insn") == 0 || strcmp(op, ".option") == 0) {
        minias_set_error(as, "unsupported-aarch64-directive:%s:line=%zu",
                         op, line);
        return false;
    }
    return true;
}

static bool code_alignment(MiniAs *as, int section, size_t amount) {
    static const unsigned char nop[] = {0x1f, 0x20, 0x03, 0xd5};
    if (amount % sizeof(nop) != 0U) {
        minias_set_error(as, "unalignable-aarch64-code-gap:%zu", amount);
        return false;
    }
    while (amount != 0U) {
        if (!minias_section_append(as, section, nop, sizeof(nop)))
            return false;
        amount -= sizeof(nop);
    }
    return true;
}

static uint32_t aarch64_fixup_relocation_type(MiniAsFixupKind kind,
                                               unsigned width) {
    switch (kind) {
        case MINIAS_FIXUP_ABS:
            return width == 4U ? AARCH64_R_ABS32 :
                   width == 8U ? AARCH64_R_ABS64 : 0U;
        case MINIAS_FIXUP_A64_CALL26:
            return width == 4U ? AARCH64_R_CALL26 : 0U;
        case MINIAS_FIXUP_A64_JUMP26:
            return width == 4U ? AARCH64_R_JUMP26 : 0U;
        default: return 0U;
    }
}

static bool validate(MiniAs *as) {
    size_t i;
    for (i = 0U; i < as->section_count; ++i) {
        const MiniAsSection *sec = &as->sections[i];
        if ((sec->flags & MINIAS_SHF_EXECINSTR) != 0U &&
            (sec->size % 4U) != 0U) {
            minias_set_error(as, "unaligned-aarch64-text:%s", sec->name);
            return false;
        }
    }
    return true;
}

static void attributes(const MiniAs *as, unsigned char elf_class,
                       MiniAsObjectAttribute *out) {
    (void)as;
    (void)elf_class;
    memset(out, 0, sizeof(*out));
}

static bool aarch64_resolve_profile(const char *march, const char *mabi,
                                    MiniAsAssemblyConfig *config,
                                    char *error, size_t capacity) {
    if ((march != NULL && strcmp(march, "armv8-a") != 0) ||
        (mabi != NULL && strcmp(mabi, "lp64") != 0)) {
        if (capacity != 0U) {
            (void)snprintf(error, capacity, "unsupported-aarch64-isa-abi");
        }
        return false;
    }
    memset(config, 0, sizeof(*config));
    config->target = minias_target_aarch64();
    return true;
}

static const MiniAsTargetOps ops = {
    .comment_prefix = '/', /* //, not #; AArch64 #42 is an immediate. */
    .elf_machine = 183U, /* EM_AARCH64 */
    .parse_target_directive = directive,
    .object_attributes = attributes,
    .name = "aarch64",
    .resolve_profile = aarch64_resolve_profile,
    .parse_mcinst = parse_mcinst,
    .measure_mcinst = aarch64_measure_mcinst,
    .encode = encode,
    .append_code_alignment = code_alignment,
    .fixup_relocation_type = aarch64_fixup_relocation_type,
    .validate_output = validate,
};
const MiniAsTargetOps *minias_target_aarch64(void) {
    return &ops;
}
