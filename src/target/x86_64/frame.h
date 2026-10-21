#ifndef MINIC_TARGET_X86_64_FRAME_H
#define MINIC_TARGET_X86_64_FRAME_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* Per-Core-value 8-byte frame slots, or 16 bytes for int128/x87 long double.
 * The extra limb is x86 backend-owned; Core IR identities are unchanged. */
typedef struct MinicX86Frame {
    size_t value_count;
    size_t *value_offsets;
    size_t *object_offsets;
    size_t object_count;
    size_t frame_size;
    size_t object_base_alignment;
    bool objects_from_aligned_rsp;
    /* SysV x86-64 va_list: gp_offset / fp_offset / overflow / reg_save.
     * Each va_start owns its own 24-byte cursor tag, while the incoming
     * argument registers have one shared 176-byte save area per function. */
    MinicCoreValueId *varargs_results;
    size_t varargs_count;
    size_t varargs_regsave_offset;
    size_t varargs_tag_base_offset;
    size_t varargs_fixed_stack_slots;
    unsigned int varargs_gp_offset;
    unsigned int varargs_fp_offset;
} MinicX86Frame;

/* Target-owned SysV stack/varargs plan, not a universal LP64 frame. */
bool minic_x86_64_align(size_t value, size_t alignment, size_t *out);
bool minic_x86_64_frame_init(const MinicC0Program *, const MinicTargetInfo *,
                            const MinicCoreFunction *, MinicX86Frame *);
void minic_x86_64_frame_destroy(MinicX86Frame *);
#endif
