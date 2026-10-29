#include "target/aarch64/asm_legalize.h"
#include "target/common/direct_call_plan.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AAPCS64 IP1 (x17) is reserved for large stack spill addressing.
 * The earlier x12 scratch silently clobbered LIVE target atomic LL/SC
 * status/compare registers during synthetic [sp,#large] rewriting.
 * x17 is not a live operand in the target structured-ASM machine
 * templates; source Core operand slots are stack materialized. */
 /* Original AArch64 large SP access and dynamic stack-base text passes. */
bool minic_aarch64_legalize_large_sp_memory(const char *path) {
    FILE *input = fopen(path, "rb"), *output, *rewritten;
    char line[512], copy[8192];
    size_t size;
    bool success = true;
    if (input == NULL) return false;
    output = tmpfile();
    if (output == NULL) { (void)fclose(input); return false; }
    while (fgets(line, sizeof(line), input) != NULL) {
        char op[16] = {0}, reg[16] = {0};
        size_t offset = 0U;
        const bool original_sp =
            sscanf(line, "  %15s %15[^,], [sp, #%zu]",
                   op, reg, &offset) == 3;
        const bool fixed_x19 = !original_sp &&
            sscanf(line, "  %15s %15[^,], [x19, #%zu]",
                   op, reg, &offset) == 3;
        if ((original_sp || fixed_x19) &&
            (strncmp(op, "ldr", 3U) == 0 ||
             strncmp(op, "str", 3U) == 0) && offset > 4095U) {
            const uint64_t imm = (uint64_t)offset;
            unsigned int shift;
            if (fprintf(output, "  movz x17, #%" PRIu64 "\n",
                        imm & UINT64_C(0xffff)) < 0) {
                success = false; break;
            }
            for (shift = 16U; shift <= 48U; shift += 16U) {
                if (((imm >> shift) & UINT64_C(0xffff)) != 0U &&
                    fprintf(output, "  movk x17, #%" PRIu64
                            ", lsl #%u\n",
                            (imm >> shift) & UINT64_C(0xffff), shift) < 0) {
                    success = false; break;
                }
            }
            if (!success ||
                fputs(fixed_x19 ? "  add x17, x19, x17\n" :
                                   "  add x17, sp, x17\n", output) == EOF ||
                fprintf(output, "  %s %s, [x17]\n", op, reg) < 0) {
                success = false; break;
            }
        } else if (fputs(line, output) == EOF) {
            success = false; break;
        }
    }
    if (ferror(input) != 0 || fclose(input) != 0) success = false;
    if (success && fseek(output, 0L, SEEK_SET) != 0) success = false;
    rewritten = success ? fopen(path, "wb") : NULL;
    if (rewritten == NULL) success = false;
    if (success) {
        while ((size = fread(copy, 1U, sizeof(copy), output)) != 0U)
            if (fwrite(copy, 1U, size, rewritten) != size) {
                success = false; break;
            }
        if (ferror(output) != 0 || fclose(rewritten) != 0) success = false;
    }
    if (fclose(output) != 0) success = false;
    return success;
}

bool minic_aarch64_copy_dynamic_frame_asm(FILE *out, FILE *input) {
    char line[1024];
    bool anchored = false;
    size_t spill = 0U;
    while (fgets(line, sizeof(line), input) != NULL) {
        char *base, *end;
        unsigned int rd, rn;
        size_t value;
        if (strchr(line, '\n') == NULL && !feof(input)) return false;
        if (strcmp(line, "  mov x19, sp\n") == 0 ||
            strcmp(line, "  mov x19, x9\n") == 0) {
            anchored = true;
        } else if (strcmp(line, "  mov sp, x19\n") == 0) {
            /* Epilogue must load the saved x19 from the restored SP.
             * This unanchors only the current RETURN terminator, not
             * subsequent textually later CFG blocks: they may be reached
             * by a branch from earlier blocks and still use dynamic SP. */
            anchored = false;
            spill = 0U;
        } else if (!anchored && strcmp(line, "  ret\n") == 0) {
            if (fputs(line, out) == EOF) return false;
            anchored = true;
            continue;
        } else if (anchored && sscanf(line, "  sub sp, sp, #%zu", &value) == 1) {
            /* Only the bounded outgoing call argument area uses sub #imm.
             * Dynamic allocations use sub sp, sp, x10, never an immediate. */
            /* Common admits 64 arguments of up to two eightbyte slots.
             * AAPCS64's 60 spilled TI pairs require 960 outgoing bytes;
             * the old 512-byte bound rejected valid dynamic-frame calls. */
            if (spill != 0U || value == 0U ||
                value > MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS * 2U * 8U)
                return false;
            spill = value;
        } else if (anchored && sscanf(line, "  add sp, sp, #%zu", &value) == 1) {
            if (spill != value) return false;
            spill = 0U;
        }
        base = anchored ? strstr(line, "[sp, #") : NULL;
        if (base != NULL) {
            unsigned long long offset;
            offset = strtoull(base + 6U, &end, 10);
            if (end == base + 6U || *end != ']' ||
                offset > (unsigned long long)MINIC_LARGE_FRAME_MAX + 512U)
                return false;
            if (offset >= (unsigned long long)spill) {
                const size_t fixed = (size_t)(offset - spill);
                if (fprintf(out, "%.*s[x19, #%zu]%s",
                            (int)(base - line), line, fixed, end + 1) < 0)
                    return false;
                continue;
            }
            /* This byte-range is the caller-owned outgoing spill area. */
        } else if (anchored &&
                   sscanf(line, "  add x%u, sp, x%u", &rd, &rn) == 2 &&
                   rd == rn && rd >= 9U && rd <= 13U) {
            if (fprintf(out, "  add x%u, x19, x%u\n", rd, rn) < 0 ||
                /* Record call arguments pre-add spill bytes to their Core
                 * object offsets. Remove exactly that adjustment after
                 * rebasing from SP to fixed x19. */
                (spill != 0U &&
                 fprintf(out, "  sub x%u, x%u, #%zu\n",
                         rd, rd, spill) < 0))
                return false;
            continue;
        }
        if (fputs(line, out) == EOF) return false;
    }
    return ferror(input) == 0 && spill == 0U;
}
