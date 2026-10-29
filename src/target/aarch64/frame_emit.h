#ifndef MINIC_TARGET_AARCH64_FRAME_EMIT_H
#define MINIC_TARGET_AARCH64_FRAME_EMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* A frame reference carries meaning, not a post-emit assembly spelling.
 * OUTGOING/PHYSICAL_SP always use the live SP, whereas owned Core/ASM
 * values use x19 once a dynamically allocated frame is anchored. */
typedef enum {
    MINIC_A64_FRAME_OWNED,
    MINIC_A64_FRAME_INCOMING,
    MINIC_A64_FRAME_OUTGOING,
    MINIC_A64_FRAME_PHYSICAL_SP
} MinicA64FrameRegion;

typedef struct {
    MinicA64FrameRegion region;
    size_t byte_offset;
} MinicA64FrameRef;

typedef struct {
    bool dynamic_frame;
    size_t frame_size;
    size_t outgoing_sp_bytes;
    /* SIZE_MAX means that no entry frame-record SP was saved. */
    size_t saved_entry_sp_offset;
} MinicA64FrameView;

typedef enum {
    MINIC_A64_REG_W,
    MINIC_A64_REG_X,
    MINIC_A64_REG_S,
    MINIC_A64_REG_D,
    MINIC_A64_REG_Q
} MinicA64FrameRegisterClass;

typedef struct {
    MinicA64FrameRegisterClass kind;
    unsigned int number;
} MinicA64FrameRegister;

typedef struct {
    /* Physical GP liveness: bit N forbids clobbering xN. The input/output
     * register of the transfer is protected automatically when GP. */
    uint32_t forbidden_gp_mask;
    /* 0 chooses x17 then x16. An explicit x9..x17 requests exactly that
     * caller-saved scratch register; the caller must forbid every live GP
     * value, including the result/source register protected automatically. */
    unsigned int preferred_scratch;
} MinicA64ScratchPolicy;

bool minic_a64_emit_frame_load(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef frame, MinicA64FrameRegister target,
    MinicA64ScratchPolicy scratch);
bool minic_a64_emit_frame_store(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef frame, MinicA64FrameRegister source,
    MinicA64ScratchPolicy scratch);
bool minic_a64_emit_frame_address(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef frame, unsigned int gp_target,
    MinicA64ScratchPolicy scratch);

/* Core SSA and record slots are always owned by the stable function frame.
 * Keep that ownership and default IP0/IP1 scratch selection in one place
 * instead of re-creating a stack-text transport in each opcode family. */
bool minic_a64_emit_owned_slot(FILE *out, const MinicA64FrameView *view,
    bool store, MinicA64FrameRegisterClass kind,
    unsigned int reg, size_t offset);
bool minic_a64_emit_owned_address(FILE *out, const MinicA64FrameView *view,
    size_t offset, unsigned int gp_target);

static inline MinicA64FrameRef minic_a64_owned_slot(size_t offset) {
    MinicA64FrameRef ref = {MINIC_A64_FRAME_OWNED, offset};
    return ref;
}

#endif
