#define _POSIX_C_SOURCE 200809L
#include "target/loongarch64/machine_text_opt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MachineTextCase {
    const char *name;
    const char *input;
    const size_t *use_counts;
    size_t value_count;
    const char *expected;
} MachineTextCase;

static bool verify_case(const MachineTextCase *test) {
    FILE *temporary = NULL, *result = NULL;
    char *first = NULL, *second = NULL;
    size_t first_length = 0U, second_length = 0U;
    bool success = false;
    temporary = open_memstream(&first, &first_length);
    if (temporary == NULL)
        goto done;
    success = minic_loongarch64_forward_single_use_spills(
        temporary, test->input, strlen(test->input),
        test->use_counts, test->value_count);
    if (fclose(temporary) != 0) success = false;
    temporary = NULL;
    if (!success) goto done;
    result = open_memstream(&second, &second_length);
    if (result == NULL)
        goto done;
    success = minic_loongarch64_emit_compact_assembly(
        result, first, first_length);
    if (fclose(result) != 0) success = false;
    result = NULL;
    if (!success) goto done;
    success = second_length == strlen(test->expected) &&
              memcmp(second, test->expected, second_length) == 0;
    if (!success) {
        (void)fprintf(stderr,
            "FAIL loong machine text %s\nexpected:\n%s\nactual:\n%s\n",
            test->name, test->expected, second != NULL ? second : "<none>");
    }
done:
    if (temporary != NULL) (void)fclose(temporary);
    if (result != NULL) (void)fclose(result);
    free(first);
    free(second);
    return success;
}

int main(void) {
    const size_t single_use[] = {1U};
    const size_t multiple_uses[] = {2U};
    const MachineTextCase tests[] = {
        {
            "single_use_dword",
            "  st.d $t0, $sp, 24\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  ld.d $t0, $sp, 24\n",
            single_use, 1U, ""
        },
        {
            "multiple_uses",
            "  st.d $t0, $sp, 24\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  ld.d $t0, $sp, 24\n",
            multiple_uses, 1U,
            "  st.d $t0, $sp, 24\n"
        },
        {
            "single_use_word_sext",
            "  st.w $t0, $s0, 88\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  ld.w $t0, $s0, 88\n",
            single_use, 1U, "  slli.w $t0, $t0, 0\n"
        },
        {
            "single_use_word_already_sext",
            "  li.w $t0, 1\n"
            "  st.w $t0, $s0, 88\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  ld.w $t0, $s0, 88\n",
            single_use, 1U, "  li.w $t0, 1\n"
        },
        {
            "parameter_dword_forward",
            "  st.d $a2, $sp, 24\n"
            "# MINI_LOONG_SSA_PARAMETER 0 2\n"
            "  ld.d $t0, $sp, 24\n",
            single_use, 1U, "  move $t0, $a2\n"
        },
        {
            "parameter_word_normalize",
            "  st.w $a3, $s0, 32\n"
            "# MINI_LOONG_SSA_PARAMETER 0 3\n"
            "  ld.w $t0, $s0, 32\n",
            single_use, 1U, "  slli.w $t0, $a3, 0\n"
        },
        {
            "cached_frame_slot",
            "  st.d $t0, $sp, 8\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  add.d $t1, $t1, $t2\n"
            "  ld.d $t0, $sp, 8\n",
            multiple_uses, 1U,
            "  st.d $t0, $sp, 8\n  add.d $t1, $t1, $t2\n"
        },
        {
            "call_barrier",
            "  st.d $t0, $sp, 8\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  bl function\n"
            "  ld.d $t0, $sp, 8\n",
            multiple_uses, 1U,
            "  st.d $t0, $sp, 8\n  bl function\n"
            "  ld.d $t0, $sp, 8\n"
        },
        {
            "asm_barrier",
            "  st.d $t0, $sp, 8\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "# MINI_LOONG_INLINE_ASM_BEGIN\n"
            "  add.d $t1, $t1, $t2\n"
            "# MINI_LOONG_INLINE_ASM_END\n"
            "  ld.d $t0, $sp, 8\n",
            multiple_uses, 1U,
            "  st.d $t0, $sp, 8\n"
            "# MINI_LOONG_INLINE_ASM_BEGIN\n"
            "  add.d $t1, $t1, $t2\n"
            "# MINI_LOONG_INLINE_ASM_END\n"
            "  ld.d $t0, $sp, 8\n"
        },
        {
            "partial_overlap_invalidates",
            "  st.d $t0, $sp, 8\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  st.w $t1, $sp, 12\n"
            "  ld.d $t0, $sp, 8\n",
            multiple_uses, 1U,
            "  st.d $t0, $sp, 8\n"
            "  st.w $t1, $sp, 12\n  ld.d $t0, $sp, 8\n"
        },
        {
            "disjoint_store_keeps_cache",
            "  st.d $t0, $sp, 8\n"
            "# MINI_LOONG_SSA_WRITEBACK 0\n"
            "  st.w $t1, $sp, 32\n"
            "  ld.d $t0, $sp, 8\n",
            multiple_uses, 1U,
            "  st.d $t0, $sp, 8\n  st.w $t1, $sp, 32\n"
        },
        {
            "fallthrough_branch",
            "  b .Lmini_loong_f0_b1\n"
            ".Lmini_loong_f0_b1:\n",
            NULL, 0U, ".Lmini_loong_f0_b1:\n"
        },
        {
            "conditional_branch_invert",
            "  bnez $t0, .Lmini_loong_f0_b2\n"
            "  b .Lmini_loong_f0_b1\n"
            ".Lmini_loong_f0_b2:\n",
            NULL, 0U,
            "  beqz $t0, .Lmini_loong_f0_b1\n.Lmini_loong_f0_b2:\n"
        },
        {
            "redundant_word_sext",
            "  li.w $t0, 3\n  slli.w $t0, $t0, 0\n",
            NULL, 0U, "  li.w $t0, 3\n"
        }
    };
    size_t index;
    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index)
        if (!verify_case(&tests[index]))
            return 1;
    (void)printf("LOONGARCH64_MACHINE_TEXT_OPT=PASS cases=%zu\n",
                 sizeof(tests) / sizeof(tests[0]));
    return 0;
}
