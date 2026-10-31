#include "target/aarch64/frame_emit.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *test_transfer(bool store, MinicA64FrameView view,
    MinicA64FrameRef ref, MinicA64FrameRegister reg,
    MinicA64ScratchPolicy scratch, bool expected) {
    FILE *out = tmpfile();
    char *buffer;
    long size;
    assert(out != NULL);
    assert((store ? minic_a64_emit_frame_store(out, &view, ref, reg, scratch)
                  : minic_a64_emit_frame_load(out, &view, ref, reg, scratch))
           == expected);
    assert(fflush(out) == 0);
    assert(fseek(out, 0L, SEEK_END) == 0);
    size = ftell(out);
    assert(size >= 0 && size < 8192);
    buffer = malloc((size_t)size + 1U);
    assert(buffer != NULL);
    rewind(out);
    assert(fread(buffer, 1U, (size_t)size, out) == (size_t)size);
    buffer[size] = '\0';
    assert(fclose(out) == 0);
    return buffer;
}

static void expect(char *output, const char *fragment) {
    if (strstr(output, fragment) == NULL) {
        (void)fprintf(stderr, "Expected %s in:\n%s\n", fragment, output);
        abort();
    }
    free(output);
}

int main(void) {
    MinicA64FrameView stable = {false, 256U, 0U, SIZE_MAX};
    MinicA64FrameView dynamic = {true, 256U, 96U, SIZE_MAX};
    MinicA64FrameView aligned = {true, 256U, 0U, 80U};
    const MinicA64ScratchPolicy free_scratch = {0U, 0U};
    const MinicA64FrameRef slot = {MINIC_A64_FRAME_OWNED, 40U};
    char *output;
    FILE *address_stream;
    expect(test_transfer(false, stable, slot,
        (MinicA64FrameRegister){MINIC_A64_REG_D, 9U},
        free_scratch, true), "ldr d9, [sp, #40]");
    expect(test_transfer(true, dynamic, slot,
        (MinicA64FrameRegister){MINIC_A64_REG_S, 9U},
        free_scratch, true), "str s9, [x19, #40]");
    stable.outgoing_sp_bytes = 96U;
    expect(test_transfer(false, stable, slot,
        (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
        free_scratch, true), "ldr x9, [sp, #136]");
    expect(test_transfer(false, dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING, 40U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
        free_scratch, true), "ldr x9, [sp, #40]");
    /* The single owned-slot API is shared by FP, memory, block/terminator
     * and record transport. It must never fall back to the live alloca SP. */
    address_stream = tmpfile();
    assert(address_stream != NULL);
    assert(minic_a64_emit_owned_slot(address_stream, &dynamic, false,
        MINIC_A64_REG_X, 9U, 4096U));
    assert(minic_a64_emit_owned_slot(address_stream, &dynamic, true,
        MINIC_A64_REG_W, 9U, 32760U));
    assert(minic_a64_emit_owned_address(address_stream, &dynamic,
        5000U, 11U));
    assert(fflush(address_stream) == 0);
    rewind(address_stream);
    {
        char emitted[1024];
        const size_t n = fread(emitted, 1U, sizeof(emitted) - 1U,
                               address_stream);
        emitted[n] = '\0';
        assert(strstr(emitted, "add x17, x19, x17") != NULL);
        assert(strstr(emitted, "ldr x9, [x17]") != NULL);
        assert(strstr(emitted, "str w9, [x17]") != NULL);
        assert(strstr(emitted, "add x11, x19, x11") != NULL);
        assert(strstr(emitted, "[sp, #4096]") == NULL);
    }
    assert(fclose(address_stream) == 0);
    expect(test_transfer(false, aligned,
        (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING, 64U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
        free_scratch, true), "ldr x9, [x17, #80]");
    expect(test_transfer(false, stable,
        (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING, 64U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
        free_scratch, true), "ldr x9, [sp, #432]");

    /* Large live x17 store must materialize the address through x16, never
     * destroy x17 with an unconditional textual large-SP fixup. This is an
     * actual AAPCS64 machine-value preservation contract. */
    stable.outgoing_sp_bytes = 0U;
    output = test_transfer(true, stable,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 32760U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 17U},
        free_scratch, true);
    assert(strstr(output, "[sp, #32760]") == NULL);
    assert(strstr(output, "movz x16, #32760") != NULL);
    assert(strstr(output, "add x16, sp, x16") != NULL);
    assert(strstr(output, "movz x17") == NULL);
    expect(output, "str x17, [x16]");
    expect(test_transfer(false, dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 4096U},
        (MinicA64FrameRegister){MINIC_A64_REG_Q, 9U},
        (MinicA64ScratchPolicy){UINT32_C(1) << 17U, 0U}, true),
        "ldr q9, [x16]");
    /* LL/SC templates may have both IP0/IP1 live yet a provably dead x12.
     * An explicit scratch request must use ONLY that register, and must
     * reject an overlap with the caller's live-register exclusion mask. */
    expect(test_transfer(false, dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 32760U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 17U},
        (MinicA64ScratchPolicy){UINT32_C(0x3ee00), 12U}, true),
        "ldr x17, [x12]");
    output = test_transfer(false, dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 32760U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 17U},
        (MinicA64ScratchPolicy){UINT32_C(0x3fe00), 12U}, false);
    assert(*output == '\0');
    free(output);
    output = test_transfer(false, dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 65520U},
        (MinicA64FrameRegister){MINIC_A64_REG_W, 9U},
        (MinicA64ScratchPolicy){
            (UINT32_C(1) << 16U) | (UINT32_C(1) << 17U), 0U}, false);
    assert(*output == '\0');
    free(output);
    stable.frame_size = SIZE_MAX;
    output = test_transfer(false, stable,
        (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING, 64U},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
        free_scratch, false);
    assert(*output == '\0');
    free(output);
    output = test_transfer(false, stable, slot,
        (MinicA64FrameRegister){MINIC_A64_REG_W, 31U},
        free_scratch, false);
    assert(*output == '\0');
    free(output);
    address_stream = tmpfile();
    assert(address_stream != NULL);
    assert(!minic_a64_emit_frame_address(address_stream, &dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 32760U},
        19U, free_scratch)); /* never destroy anchored x19 */
    assert(minic_a64_emit_frame_address(address_stream, &dynamic,
        (MinicA64FrameRef){MINIC_A64_FRAME_OWNED, 32760U},
        10U, free_scratch));
    assert(fclose(address_stream) == 0);
    (void)puts("AARCH64_TYPED_FRAME_REF=PASS");
    return 0;
}
