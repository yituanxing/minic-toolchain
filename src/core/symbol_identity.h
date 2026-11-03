#ifndef MINIC_CORE_SYMBOL_IDENTITY_H
#define MINIC_CORE_SYMBOL_IDENTITY_H

/* A sealed machine-local symbol is a self-contained identity: machine
 * emitters never reopen a frontend statement or inline-asm table to recover
 * its name. These strictly decimal namespaces are allocated at source ingress
 * and remain valid after the C program has been released. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A conservative, portable assembler-name lexical subset. This prevents
 * source-controlled names from becoming directives or extra operands before
 * any target dialect formats them. It does NOT claim that all legal ELF,
 * COFF or Mach-O symbols belong to this subset, nor does it replace the
 * target's relocation/binding/section admission policy. */
static inline bool minic_core_assembler_symbol_lexically_safe(const char *symbol) {
    const unsigned char *cursor = (const unsigned char *)symbol;
    if (cursor == NULL || *cursor == '\0') return false;
    for (; *cursor != '\0'; ++cursor) {
        const unsigned char c = *cursor;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$'))
            return false;
    }
    return true;
}

static inline bool minic_core_decimal_symbol_valid(const char *name,
                                                    const char *prefix,
                                                    const char *suffix) {
    const char *cursor;
    size_t length;
    size_t ordinal = 0U;
    if (name == NULL || prefix == NULL || suffix == NULL) return false;
    length = strlen(prefix);
    if (strncmp(name, prefix, length) != 0) return false;
    cursor = name + length;
    if (*cursor < '0' || *cursor > '9') return false;
    /* Names are canonical: no leading zero, whitespace, or assembler syntax. */
    if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') return false;
    do {
        const size_t digit = (size_t)(*cursor - '0');
        if (ordinal > (SIZE_MAX - digit) / 10U) return false;
        ordinal = ordinal * 10U + digit;
        ++cursor;
    } while (*cursor >= '0' && *cursor <= '9');
    return strcmp(cursor, suffix) == 0;
}

static inline bool minic_core_label_symbol_valid(const char *name) {
    return minic_core_decimal_symbol_valid(name, ".Luser_", "");
}

static inline bool minic_core_deferred_asm_symbol_valid(const char *name) {
    return minic_core_decimal_symbol_valid(
        name, "__minic_deferred_asm_immediate_", "_0");
}

static inline bool minic_core_format_symbol(const char *format, size_t ordinal,
                                            char **result) {
    char *allocated;
    int length;
    if (format == NULL || result == NULL) return false;
    *result = NULL;
    length = snprintf(NULL, 0U, format, ordinal);
    if (length < 0 || (size_t)length == SIZE_MAX) return false;
    allocated = malloc((size_t)length + 1U);
    if (allocated == NULL) return false;
    if (snprintf(allocated, (size_t)length + 1U, format, ordinal) != length) {
        free(allocated);
        return false;
    }
    *result = allocated;
    return true;
}

static inline bool minic_core_format_label_symbol(size_t ordinal, char **result) {
    return minic_core_format_symbol(".Luser_%zu", ordinal, result);
}

static inline bool minic_core_format_deferred_asm_symbol(size_t ordinal,
                                                          char **result) {
    return minic_core_format_symbol("__minic_deferred_asm_immediate_%zu_0",
                                    ordinal, result);
}

#endif
