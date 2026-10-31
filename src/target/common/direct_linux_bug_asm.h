#ifndef MINIC_TARGET_COMMON_DIRECT_LINUX_BUG_ASM_H
#define MINIC_TARGET_COMMON_DIRECT_LINUX_BUG_ASM_H
#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Shared GNU ELF metadata directive family, with target-supplied labels
 * and native trap. Never admit arbitrary machine instructions or .inst. */
static inline bool minic_multi_direct_linux_bug_asm(
    const MinicCoreInlineAsm *a, const char *bug_label,
    const char *exit_label, const char *file_label,
    const char *native_trap, bool final_align) {
    char compact[1024], prefix[300], after_file[128], suffix[160];
    const char *cursor;
    size_t i, n = 0U, path_chars = 0U, digits = 0U, flag_chars = 0U;
    int size;
    if (a == NULL || !a->is_volatile || a->is_goto ||
        a->has_memory_clobber || a->register_clobber_count != 0U ||
        a->template_text == NULL || a->template_length < 100U ||
        a->template_length >= sizeof(compact) ||
        bug_label == NULL || exit_label == NULL ||
        file_label == NULL || native_trap == NULL)
        return false;
    for (i = 0U; i < a->template_length; ++i) {
        const char c = a->template_text[i];
        if (c == '\0') return false;
        /* Kbuild bug-table file paths have no embedded whitespace. */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        compact[n++] = c;
    }
    compact[n] = '\0';
    size = snprintf(prefix, sizeof(prefix),
        ".pushsection__bug_table,\"aw\";.align2;"
        "%s:.long%sf-.;"
        ".pushsection.rodata.str,\"aMS\",@progbits,1;"
        "%s:.string\"",
        bug_label, exit_label, file_label);
    if (size <= 0 || (size_t)size >= sizeof(prefix) ||
        strncmp(compact, prefix, (size_t)size) != 0)
        return false;
    cursor = compact + (size_t)size;
    while ((cursor[path_chars] >= 'a' && cursor[path_chars] <= 'z') ||
           (cursor[path_chars] >= 'A' && cursor[path_chars] <= 'Z') ||
           (cursor[path_chars] >= '0' && cursor[path_chars] <= '9') ||
           cursor[path_chars] == '/' || cursor[path_chars] == '.' ||
           cursor[path_chars] == '_' || cursor[path_chars] == '-') {
        ++path_chars;
        if (path_chars > 220U) return false;
    }
    if (path_chars == 0U || cursor[path_chars] != '\"') return false;
    cursor += path_chars + 1U;
    size = snprintf(after_file, sizeof(after_file),
                    ";.popsection;.long%sb-.;.short", file_label);
    if (size <= 0 || (size_t)size >= sizeof(after_file) ||
        strncmp(cursor, after_file, (size_t)size) != 0) return false;
    cursor += (size_t)size;
    while (cursor[digits] >= '0' && cursor[digits] <= '9') ++digits;
    if (digits == 0U || digits > 6U || cursor[digits] != ';')
        return false;
    cursor += digits;
    if (strncmp(cursor, ";.short", 7U) != 0) return false;
    cursor += 7U;
    while ((cursor[flag_chars] >= '0' && cursor[flag_chars] <= '9') ||
           cursor[flag_chars] == '(' || cursor[flag_chars] == ')' ||
           cursor[flag_chars] == '<' || cursor[flag_chars] == '>' ||
           cursor[flag_chars] == '|' || cursor[flag_chars] == '&' ||
           cursor[flag_chars] == '+' || cursor[flag_chars] == '-' ||
           cursor[flag_chars] == '*' || cursor[flag_chars] == '/') {
        ++flag_chars;
        if (flag_chars > 120U) return false;
    }
    if (flag_chars == 0U) return false;
    cursor += flag_chars;
    size = snprintf(suffix, sizeof(suffix),
                    "%s;.popsection;%s:%s",
                    final_align ? ";.align2" : "", exit_label, native_trap);
    if (size <= 0 || (size_t)size >= sizeof(suffix)) return false;
    return strcmp(cursor, suffix) == 0;
}
#endif
