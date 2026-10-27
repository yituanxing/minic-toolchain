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

static bool measure(const MiniAs *as, const char *op, const char *args,
                    uint32_t *size, char *reason, size_t cap) {
    (void)as;
    (void)args;
    if (strcmp(op, "nop") == 0 || strcmp(op, "ret") == 0 ||
        strcmp(op, "mov") == 0 || strcmp(op, "svc") == 0 ||
        strcmp(op, "b") == 0 || strcmp(op, "bl") == 0) {
        *size = 4U;
        return true;
    }
    (void)snprintf(reason, cap, "unsupported-aarch64-instruction:%s", op);
    return false;
}

static bool append_word(MiniAs *as, int section, uint32_t word) {
    unsigned char bytes[4];
    unsigned i;
    for (i = 0U; i != 4U; ++i)
        bytes[i] = (unsigned char)(word >> (i * 8U));
    return minias_section_append(as, section, bytes, sizeof(bytes));
}

static bool encode(MiniAs *as, const MiniAsStmt *stmt) {
    uint32_t code = 0U;
    const char *op = stmt->op;
    const char *args = stmt->args;
    unsigned rd, imm;
    bool is_64;
    char operand1[80], operand2[80];
    MiniAsSymbolExpr expr;
    uint32_t reloc = 0U;
    if (strcmp(op, "nop") == 0 && *args == '\0') {
        code = UINT32_C(0xd503201f);
    } else if (strcmp(op, "ret") == 0 && *args == '\0') {
        code = UINT32_C(0xd65f03c0);
    } else if (strcmp(op, "ret") == 0 &&
               parse_register(args, &rd, &is_64) && is_64) {
        code = UINT32_C(0xd65f0000) | (rd << 5U);
    } else if (strcmp(op, "mov") == 0 &&
               split_two(args, operand1, sizeof(operand1),
                         operand2, sizeof(operand2)) &&
               parse_register(operand1, &rd, &is_64) &&
               parse_imm16(operand2, &imm)) {
        code = (is_64 ? UINT32_C(0xd2800000) : UINT32_C(0x52800000)) |
               ((uint32_t)imm << 5U) | rd;
    } else if (strcmp(op, "svc") == 0 && parse_imm16(args, &imm)) {
        code = UINT32_C(0xd4000001) | ((uint32_t)imm << 5U);
    } else if ((strcmp(op, "b") == 0 || strcmp(op, "bl") == 0) &&
               minias_parse_symbol_addend(args, &expr)) {
        code = strcmp(op, "bl") == 0 ?
               UINT32_C(0x94000000) : UINT32_C(0x14000000);
        reloc = strcmp(op, "bl") == 0 ? AARCH64_R_CALL26 : AARCH64_R_JUMP26;
        if ((expr.addend & 3) != 0) {
            minias_set_error(as, "bad-aarch64-branch-addend:%s:line=%zu",
                             args, stmt->line);
            return false;
        }
    } else {
        minias_set_error(as, "bad-aarch64-operands:%s:%s:line=%zu",
                         op, args, stmt->line);
        return false;
    }
    if (!append_word(as, stmt->section, code)) return false;
    if (reloc != 0U &&
        !minias_add_relocation(as, stmt->section, stmt->offset, reloc,
                              expr.name, expr.addend)) return false;
    return true;
}

static bool directive(MiniAs *as, const char *op, const char *args,
                      size_t line, bool *handled) {
    (void)args;
    *handled = false;
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

static uint32_t relocation_type(unsigned width, MiniAsRelocKind kind) {
    if (kind != MINIAS_RELOC_ABS) return 0U;
    return width == 4U ? AARCH64_R_ABS32 :
           width == 8U ? AARCH64_R_ABS64 : 0U;
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

static bool no_conditional_branch(const char *op) {
    (void)op;
    return false;
}

static const MiniAsTargetOps ops = {
    .comment_prefix = '/', /* //, not #; AArch64 #42 is an immediate. */
    .elf_machine = 183U, /* EM_AARCH64 */
    .parse_target_directive = directive,
    .object_attributes = attributes,
    .name = "aarch64",
    .measure = measure,
    .encode = encode,
    .is_conditional_branch = no_conditional_branch,
    .append_code_alignment = code_alignment,
    .relocation_type = relocation_type,
    .validate_output = validate,
};
const MiniAsTargetOps *minias_target_aarch64(void) {
    return &ops;
}
