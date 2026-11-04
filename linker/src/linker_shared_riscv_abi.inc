/* Machine-owned slot geometry for the currently supported RV64 ET_DYN ABI.
 * Shared mode owns whether symbols need GOT, PLT and TLS-GD reservations;
 * the target owns the byte layout of each reservation.  This is deliberately
 * not an assertion that other architectures share RV64's eager PLT recipe. */
typedef struct MiniLdSharedTargetAbi {
    size_t got_slot_bytes;
    size_t tls_gd_slot_bytes;
    size_t plt_entry_bytes;
    size_t gotplt_header_slots;
    size_t gotplt_slot_bytes;
} MiniLdSharedTargetAbi;

static const MiniLdSharedTargetAbi minild_riscv64_shared_abi = {
    8U, 16U, 16U, 3U, 8U
};

static const MiniLdSharedTargetAbi *
minild_shared_target_abi(const MiniLdState *state) {
    if (state->target != NULL &&
        state->target->shared_backend == MINILD_SHARED_RISCV_ELF64) {
        return &minild_riscv64_shared_abi;
    }
    return NULL;
}
