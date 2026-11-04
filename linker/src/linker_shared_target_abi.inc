/* Shared ET_DYN target interface.  Preemption, dynamic-symbol identity,
 * liveness and relocation reservation counts are shared-link decisions.
 * The selected target supplies relocation semantics, wire record widths,
 * allocation geometry and the final instruction/PLT fixup stages.  A new
 * machine implements a new leaf; it does not change the shared pipeline. */
struct MiniLdSharedImage;

typedef struct MiniLdSharedTargetAbi {
    size_t got_slot_bytes;
    size_t tls_gd_slot_bytes;
    size_t plt_entry_bytes;
    size_t gotplt_header_slots;
    size_t gotplt_slot_bytes;
    size_t dynsym_record_bytes;
    size_t rela_record_bytes;
    size_t dynamic_record_bytes;
    MiniLdTargetRelocClass (*classify_relocation)(uint32_t);
    bool (*relocation_at_final_layout)(uint32_t);
    Elf64_Xword (*dynamic_relocation_info)(
        size_t, MiniLdDynamicRelocSemantic);
    int64_t (*addend_bits)(uint64_t, int64_t);
    size_t (*absolute_relocation_width)(MiniLdTargetRelocClass);
    void (*store_absolute)(unsigned char *, MiniLdTargetRelocClass, uint64_t);
    bool (*patch_final_relocations)(
        MiniLdState *, const MiniLdLayout *, const MiniLdOutputPlan *,
        const struct MiniLdSharedImage *);
    bool (*finalize_plt_and_calls)(
        MiniLdState *, const MiniLdLayout *, const MiniLdOutputPlan *,
        const struct MiniLdSharedImage *);
} MiniLdSharedTargetAbi;

#include "linker_shared_riscv_abi.inc"

/* TargetProfile selects the implementation; Common does not enumerate ISA
 * identities here.  RV32 compat-vDSO deliberately has no full ET_DYN ABI. */
static const MiniLdSharedTargetAbi *
minild_shared_target_abi(const MiniLdState *state) {
    return state != NULL && state->target != NULL &&
           state->target->shared_abi != NULL
               ? state->target->shared_abi() : NULL;
}
