/* RV32 Linux 6.6.143 virtio_pci_modern_dev.c::vp_modern_get_features
 * regression.  VIRTIO_F_VERSION_1 is bit 32, not bit 0; losing the
 * compound-assignment writeback makes a modern PCI virtio device appear
 * to lack this mandatory feature even when its high feature register is 1.
 *
 * Compile this exact GNU-preprocessed C both with GNU C and MiniC C, then
 * link the resulting ELF32 objects to the same GNU-built driver and run
 * both under actual qemu-riscv32 (run-rv32-u64-compound-or-store.sh).
 */

typedef unsigned int rv32_u32;
typedef unsigned long long rv32_u64;
typedef rv32_u64 (*rv32_feature_fn)(rv32_u32, rv32_u32);

rv32_u64 rv32_virtio_feature_rmw(rv32_u32 low, rv32_u32 high) {
    rv32_u64 features = low;
    features |= ((rv32_u64)high << 32);
    return features;
}

rv32_u64 rv32_virtio_feature_explicit(rv32_u32 low, rv32_u32 high) {
    rv32_u64 features = low;
    features = features | ((rv32_u64)high << 32);
    return features;
}

/* Indirect 64-bit return tests the caller-side RV32 a0/a1 ABI independently
 * of the 64-bit OR-and-writeback logic in the function above. */
rv32_u64 rv32_virtio_feature_indirect(rv32_feature_fn read_features,
                                      rv32_u32 low, rv32_u32 high) {
    return read_features(low, high);
}

int rv32_virtio_has_version_1(rv32_u32 low, rv32_u32 high) {
    return !!(rv32_virtio_feature_rmw(low, high) & (1ULL << 32));
}

/* Faithful transport-feature copy from Linux 6.6.143 virtio.c::
 * virtio_dev_probe(): i walks [VIRTIO_TRANSPORT_F_START=28,
 * VIRTIO_TRANSPORT_F_END=41), and __virtio_set_bit() performs a 64-bit
 * compound OR into a struct member.  This is a separate failure surface
 * from vp_modern_get_features() itself. */
struct rv32_virtio_device {
    rv32_u32 canary;
    rv32_u64 features;
};

rv32_u64 rv32_virtio_negotiated_transport(rv32_u32 low, rv32_u32 high) {
    struct rv32_virtio_device dev;
    rv32_u64 device_features = rv32_virtio_feature_rmw(low, high);
    unsigned int i;
    dev.canary = 0x45671234U;
    dev.features = 0;
    for (i = 28; i < 41; ++i)
        if (device_features & (1ULL << i))
            dev.features |= 1ULL << i;
    if (dev.canary != 0x45671234U)
        return 0xffffffffffffffffULL;
    return dev.features;
}
