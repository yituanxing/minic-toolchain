#include "target/aarch64/frame_emit.h"
#include "target/common/lp64_frame.h"

static bool a64_add(size_t lhs, size_t rhs, size_t *result) {
    if (result == NULL || lhs > SIZE_MAX - rhs) return false;
    *result = lhs + rhs;
    return true;
}

bool minic_a64_resolve_frame(const MinicA64FrameView *view,
                            MinicA64FrameRef ref, MinicA64ResolvedFrame *out) {
    if (view == NULL || out == NULL) return false;
    out->saved_entry_frame_sp = false;
    switch (ref.region) {
    case MINIC_A64_FRAME_OWNED:
        /* Reject an unmapped Core SSA sentinel before any address
         * materialization. Owned offsets must fit the shared LP64 plan. */
        if (ref.byte_offset > MINIC_LARGE_FRAME_MAX) return false;
        out->base_register = view->dynamic_frame ? 19U : 32U;
        if (!view->dynamic_frame &&
            !a64_add(ref.byte_offset, view->outgoing_sp_bytes,
                     &ref.byte_offset)) return false;
        out->displacement = ref.byte_offset;
        return true;
    case MINIC_A64_FRAME_OUTGOING:
    case MINIC_A64_FRAME_PHYSICAL_SP:
        out->base_register = 32U;
        out->displacement = ref.byte_offset;
        return true;
    case MINIC_A64_FRAME_INCOMING:
        if (!a64_add(ref.byte_offset, 16U, &ref.byte_offset))
            return false;
        if (view->saved_entry_sp_offset != SIZE_MAX) {
            if (!view->dynamic_frame) return false;
            out->saved_entry_frame_sp = true;
            out->base_register = 19U;
            out->displacement = ref.byte_offset;
            return true;
        }
        if (!a64_add(ref.byte_offset, view->frame_size, &ref.byte_offset) ||
            (!view->dynamic_frame &&
             !a64_add(ref.byte_offset, view->outgoing_sp_bytes,
                      &ref.byte_offset))) return false;
        out->base_register = view->dynamic_frame ? 19U : 32U;
        out->displacement = ref.byte_offset;
        return true;
    }
    return false;
}
