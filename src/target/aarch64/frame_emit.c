#include "target/aarch64/frame_emit.h"
#include <inttypes.h>
#include <string.h>

typedef struct {
    const char *base;
    size_t displacement;
    bool saved_entry_frame_sp;
} MinicA64ResolvedFrame;

static bool a64_add(size_t lhs, size_t rhs, size_t *result) {
    if (result == NULL || lhs > SIZE_MAX - rhs) return false;
    *result = lhs + rhs;
    return true;
}

static bool a64_resolve(const MinicA64FrameView *view,
                        MinicA64FrameRef ref, MinicA64ResolvedFrame *out) {
    if (view == NULL || out == NULL) return false;
    out->saved_entry_frame_sp = false;
    switch (ref.region) {
    case MINIC_A64_FRAME_OWNED:
    case MINIC_A64_FRAME_ASM_OPERAND:
        out->base = view->dynamic_frame ? "x19" : "sp";
        if (!view->dynamic_frame &&
            !a64_add(ref.byte_offset, view->outgoing_sp_bytes,
                     &ref.byte_offset)) return false;
        out->displacement = ref.byte_offset;
        return true;
    case MINIC_A64_FRAME_OUTGOING:
    case MINIC_A64_FRAME_PHYSICAL_SP:
        out->base = "sp";
        out->displacement = ref.byte_offset;
        return true;
    case MINIC_A64_FRAME_INCOMING:
        if (!a64_add(ref.byte_offset, 16U, &ref.byte_offset))
            return false;
        if (view->saved_entry_sp_offset != SIZE_MAX) {
            if (!view->dynamic_frame) return false;
            out->saved_entry_frame_sp = true;
            out->base = "x19";
            out->displacement = ref.byte_offset;
            return true;
        }
        if (!a64_add(ref.byte_offset, view->frame_size, &ref.byte_offset) ||
            (!view->dynamic_frame &&
             !a64_add(ref.byte_offset, view->outgoing_sp_bytes,
                      &ref.byte_offset))) return false;
        out->base = view->dynamic_frame ? "x19" : "sp";
        out->displacement = ref.byte_offset;
        return true;
    }
    return false;
}

static unsigned int a64_scratch(MinicA64ScratchPolicy policy,
                                uint32_t exclusions) {
    uint32_t used = policy.forbidden_gp_mask | exclusions;
    unsigned int preferred = policy.preferred_scratch;
    if (preferred != 0U && preferred != 16U && preferred != 17U)
        return 0U;
    if (preferred == 0U) preferred = 17U;
    if ((used & (UINT32_C(1) << preferred)) == 0U)
        return preferred;
    if (policy.preferred_scratch != 0U) return 0U;
    preferred = preferred == 17U ? 16U : 17U;
    return (used & (UINT32_C(1) << preferred)) == 0U ? preferred : 0U;
}

static bool a64_materialize(FILE *out, unsigned int gp, size_t displacement) {
    unsigned int shift;
    const uint64_t value = (uint64_t)displacement;
    if (out == NULL || gp > 30U ||
        (size_t)value != displacement ||
        fprintf(out, "  movz x%u, #%" PRIu64 "\n",
                gp, value & UINT64_C(65535)) < 0)
        return false;
    for (shift = 16U; shift < 64U; shift += 16U)
        if (((value >> shift) & UINT64_C(65535)) != 0U &&
            fprintf(out, "  movk x%u, #%" PRIu64 ", lsl #%u\n",
                    gp, (value >> shift) & UINT64_C(65535), shift) < 0)
            return false;
    return true;
}

static bool a64_address_from_base(FILE *out, unsigned int gp,
                                  const char *base, size_t displacement) {
    /* Materializing an offset into the address register would destroy the
     * base before the ADD when both registers alias. This is particularly
     * dangerous for the dynamic frame's callee-saved x19 anchor. */
    if ((gp == 19U && strcmp(base, "x19") == 0) ||
        (gp == 16U && strcmp(base, "x16") == 0) ||
        (gp == 17U && strcmp(base, "x17") == 0))
        return false;
    if (displacement == 0U)
        return fprintf(out, "  mov x%u, %s\n", gp, base) >= 0;
    if (displacement <= 4095U)
        return fprintf(out, "  add x%u, %s, #%zu\n",
                       gp, base, displacement) >= 0;
    return a64_materialize(out, gp, displacement) &&
        fprintf(out, "  add x%u, %s, x%u\n", gp, base, gp) >= 0;
}

static bool a64_reg_shape(MinicA64FrameRegister reg,
                          const char **prefix, size_t *width) {
    if (reg.number > 31U || prefix == NULL || width == NULL) return false;
    switch (reg.kind) {
    case MINIC_A64_REG_W: *prefix = "w"; *width = 4U; return reg.number < 31U;
    case MINIC_A64_REG_X: *prefix = "x"; *width = 8U; return reg.number < 31U;
    case MINIC_A64_REG_S: *prefix = "s"; *width = 4U; return true;
    case MINIC_A64_REG_D: *prefix = "d"; *width = 8U; return true;
    case MINIC_A64_REG_Q: *prefix = "q"; *width = 16U; return true;
    }
    return false;
}

static bool a64_saved_entry_sp(FILE *out, const MinicA64FrameView *view,
                               unsigned int into, unsigned int address) {
    const size_t offset = view->saved_entry_sp_offset;
    if ((offset % 8U) == 0U && offset <= 4095U)
        return fprintf(out, "  ldr x%u, [x19, #%zu]\n",
                       into, offset) >= 0;
    if (into == address ||
        !a64_address_from_base(out, address, "x19", offset))
        return false;
    return fprintf(out, "  ldr x%u, [x%u]\n", into, address) >= 0;
}

static bool a64_transfer(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef ref, MinicA64FrameRegister reg,
    MinicA64ScratchPolicy policy, bool is_store) {
    MinicA64ResolvedFrame resolved;
    const char *prefix, *operation = is_store ? "str" : "ldr";
    size_t width;
    uint32_t excluded = 0U;
    unsigned int scratch, secondary;
    if (out == NULL || !a64_reg_shape(reg, &prefix, &width) ||
        !a64_resolve(view, ref, &resolved)) return false;
    if (reg.kind == MINIC_A64_REG_W || reg.kind == MINIC_A64_REG_X)
        excluded = UINT32_C(1) << reg.number;
    /* A64's native scaled immediate accepts 4095 * width bytes, but the
     * remaining whole-program text legalizer still rewrites every
     * [sp|x19,#offset] above 4095 bytes. Until ALL frame emitters migrate,
     * materialize those offsets here to avoid its unadvertised x17 clobber.
     * This compatibility gate can be removed with the legacy finalizer. */
    if (!resolved.saved_entry_frame_sp &&
        resolved.displacement % width == 0U &&
        resolved.displacement <= 4095U)
        return fprintf(out, "  %s %s%u, [%s, #%zu]\n",
                       operation, prefix, reg.number,
                       resolved.base, resolved.displacement) >= 0;
    scratch = a64_scratch(policy, excluded);
    if (scratch == 0U) return false;
    if (resolved.saved_entry_frame_sp) {
        MinicA64ScratchPolicy secondary_policy = policy;
        secondary_policy.preferred_scratch = 0U;
        secondary = a64_scratch(secondary_policy,
            excluded | (UINT32_C(1) << scratch));
        if (resolved.displacement % width == 0U &&
            resolved.displacement <= 4095U &&
            view->saved_entry_sp_offset % 8U == 0U &&
            view->saved_entry_sp_offset <= 4095U)
            return a64_saved_entry_sp(out, view, scratch, 0U) &&
                fprintf(out, "  %s %s%u, [x%u, #%zu]\n",
                        operation, prefix, reg.number,
                        scratch, resolved.displacement) >= 0;
        if (secondary == 0U ||
            !a64_saved_entry_sp(out, view, scratch, secondary) ||
            !a64_address_from_base(out, secondary,
                scratch == 16U ? "x16" : "x17", resolved.displacement))
            return false;
        return fprintf(out, "  %s %s%u, [x%u]\n",
                       operation, prefix, reg.number, secondary) >= 0;
    }
    return a64_address_from_base(out, scratch, resolved.base,
                                 resolved.displacement) &&
        fprintf(out, "  %s %s%u, [x%u]\n",
                operation, prefix, reg.number, scratch) >= 0;
}

bool minic_a64_emit_frame_load(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef frame, MinicA64FrameRegister target,
    MinicA64ScratchPolicy scratch) {
    return a64_transfer(out, view, frame, target, scratch, false);
}

bool minic_a64_emit_frame_store(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef frame, MinicA64FrameRegister source,
    MinicA64ScratchPolicy scratch) {
    return a64_transfer(out, view, frame, source, scratch, true);
}

bool minic_a64_emit_frame_address(FILE *out, const MinicA64FrameView *view,
    MinicA64FrameRef frame, unsigned int gp_target,
    MinicA64ScratchPolicy policy) {
    MinicA64ResolvedFrame resolved;
    unsigned int scratch;
    if (out == NULL || gp_target > 30U ||
        (view != NULL && view->dynamic_frame && gp_target == 19U) ||
        !a64_resolve(view, frame, &resolved)) return false;
    if (!resolved.saved_entry_frame_sp)
        return a64_address_from_base(out, gp_target, resolved.base,
                                     resolved.displacement);
    scratch = a64_scratch(policy, UINT32_C(1) << gp_target);
    if (scratch == 0U ||
        !a64_saved_entry_sp(out, view, scratch, gp_target))
        return false;
    return a64_address_from_base(out, gp_target,
        scratch == 16U ? "x16" : "x17", resolved.displacement);
}

bool minic_a64_emit_owned_slot(FILE *out, const MinicA64FrameView *view,
    bool store, MinicA64FrameRegisterClass kind,
    unsigned int reg, size_t offset) {
    const MinicA64FrameRegister operand = {kind, reg};
    const MinicA64ScratchPolicy scratch = {0U, 0U};
    return store ? minic_a64_emit_frame_store(out, view,
                       minic_a64_owned_slot(offset), operand, scratch)
                 : minic_a64_emit_frame_load(out, view,
                       minic_a64_owned_slot(offset), operand, scratch);
}

bool minic_a64_emit_owned_address(FILE *out, const MinicA64FrameView *view,
    size_t offset, unsigned int gp_target) {
    const MinicA64ScratchPolicy scratch = {0U, 0U};
    return minic_a64_emit_frame_address(out, view,
        minic_a64_owned_slot(offset), gp_target, scratch);
}
