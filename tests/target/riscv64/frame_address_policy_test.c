/* Standalone native unit test for the target-owned RISC-V frame policy.
 * Run: cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
 *      -Isrc -Iinclude tests/target/riscv64/frame_address_policy_test.c \
 *      src/target/riscv64/frame_access.c -Wl,--gc-sections \
 *      -o /tmp/rv-frame-policy-test
 *      /tmp/rv-frame-policy-test
 * This checks assembler text rather than host-executing target instructions. */
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv/function_symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Mock only the Core frame-layout lookup: pair emission stays production code.
 * Using a value id of 4096 models a large frame slot. */
bool minic_riscv64_core_frame_value_offset(
    const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id,
    size_t *offset) {
    if (frame == NULL || offset == NULL) return false;
    *offset = (size_t)value_id;
    return true;
}

static bool emitted_contains(FILE *file, const char *required,
                             const char *forbidden) {
    char buffer[2048];
    size_t count;
    if (file == NULL || fflush(file) != 0 || fseek(file, 0L, SEEK_SET) != 0)
        return false;
    count = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    if (ferror(file)) return false;
    buffer[count] = '\0';
    return strstr(buffer, required) != NULL &&
           (forbidden == NULL || strstr(buffer, forbidden) == NULL);
}

static bool emitted_pair_in_order(FILE *file, const char *first,
                                  const char *second, const char *forbidden) {
    char buffer[2048];
    const char *first_position;
    const char *second_position;
    size_t count;
    if (file == NULL || fflush(file) != 0 || fseek(file, 0L, SEEK_SET) != 0)
        return false;
    count = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    if (ferror(file)) return false;
    buffer[count] = '\0';
    first_position = strstr(buffer, first);
    second_position = strstr(buffer, second);
    return first_position != NULL && second_position != NULL &&
           first_position < second_position &&
           (forbidden == NULL || strstr(buffer, forbidden) == NULL);
}

static bool frame_word_case(bool dynamic_frame, bool store,
                            const char *register_name, size_t offset,
                            const char *required, const char *forbidden) {
    const MinicRiscvFrameAddressPolicy policy =
        minic_riscv64_machine_frame_policy(dynamic_frame);
    FILE *file = tmpfile();
    bool ok = file != NULL &&
        minic_riscv_frame_emit_word(file, &policy, store, register_name, offset) &&
        emitted_contains(file, required, forbidden);
    if (file != NULL) (void)fclose(file);
    return ok;
}

static bool frame_address_case(bool dynamic_frame, const char *register_name,
                               size_t offset, const char *required) {
    const MinicRiscvFrameAddressPolicy policy =
        minic_riscv64_machine_frame_policy(dynamic_frame);
    FILE *file = tmpfile();
    bool ok = file != NULL &&
        minic_riscv_frame_emit_address(file, &policy, register_name, offset) &&
        emitted_contains(file, required, NULL);
    if (file != NULL) (void)fclose(file);
    return ok;
}

static bool symbol_default_case(void) {
    MinicRiscvFunctionSymbol symbol;
    FILE *file = tmpfile();
    bool ok = minic_riscv_function_symbol_for_name("frame_policy_test", &symbol) &&
        symbol.explicit_alignment == 0U &&
        symbol.section_name == NULL && !symbol.function_sections &&
        !symbol.is_internal && !symbol.is_weak &&
        symbol.visibility == MINIC_SYMBOL_VISIBILITY_DEFAULT &&
        file != NULL && minic_riscv_emit_function_symbol_begin(file, &symbol) &&
        emitted_contains(file, ".type frame_policy_test, @function\n",
                         ".balign ");
    if (file != NULL) (void)fclose(file);
    return ok;
}

static bool frame_pair_case(bool dynamic_frame, bool store,
                            const char *low, const char *high,
                            const char *first_expected,
                            const char *second_expected,
                            const char *forbidden) {
    MinicRiscv64CoreFrame frame = {0};
    FILE *file = tmpfile();
    bool ok;
    frame.has_dynamic_stack_alloc = dynamic_frame;
    ok = file != NULL &&
         (store ? minic_riscv64_frame_store_int128(file, &frame, 4096U, low, high) :
                  minic_riscv64_frame_load_int128(file, &frame, 4096U, low, high)) &&
         emitted_pair_in_order(file, first_expected, second_expected, forbidden);
    if (file != NULL) (void)fclose(file);
    return ok;
}

int main(void) {
    MinicRiscvFrameAddressPolicy impossible = {
        "sp", "t2", "t2", "ld", "sd"
    };
    FILE *file = tmpfile();
    bool fail_closed = file != NULL &&
        !minic_riscv_frame_emit_word(file, &impossible, true, "t2", 4096U);
    if (file != NULL) (void)fclose(file);

    if (!frame_word_case(false, true, "t2", 2047U,
                         "sd t2, 2047(sp)", "li t2,") ||
        !frame_word_case(false, true, "t2", 2048U,
                         "sd t2, 0(t6)", "li t2,") ||
        !frame_word_case(false, false, "t2", 2048U,
                         "ld t2, 0(t6)", "li t2,") ||
        !frame_word_case(false, true, "t0", 4096U,
                         "sd t0, 0(t2)", NULL) ||
        !frame_word_case(true, true, "t6", 4096U,
                         "sd t6, 0(t5)", "li t6,") ||
        !frame_word_case(true, false, "t6", 4096U,
                         "ld t6, 0(t5)", "li t6,") ||
        !frame_address_case(false, "t3", 2048U,
                            "add t3, sp, t3") ||
        !frame_address_case(true, "t6", 2048U,
                            "add t6, s0, t6") ||
        !frame_pair_case(false, true, "t1", "t2",
                         "sd t1, 0(t6)", "sd t2, 0(t6)", "li t2,") ||
        !frame_pair_case(false, false, "t1", "t2",
                         "ld t1, 0(t2)", "ld t2, 0(t6)", NULL) ||
        !frame_pair_case(false, false, "t2", "t3",
                         "ld t2, 0(t6)", "ld t3, 0(t6)", "li t2,") ||
        !frame_pair_case(true, true, "t1", "t2",
                         "sd t1, 0(t6)", "sd t2, 0(t6)", "li t2,") ||
        !frame_pair_case(true, false, "t1", "t2",
                         "ld t1, 0(t6)", "ld t2, 0(t6)", "li t2,") ||
        !fail_closed || !symbol_default_case()) {
        (void)fprintf(stderr, "RISC-V machine frame address policy regression FAIL\n");
        return 1;
    }
    (void)puts("RISC-V machine frame address policy regression PASS");
    return 0;
}
