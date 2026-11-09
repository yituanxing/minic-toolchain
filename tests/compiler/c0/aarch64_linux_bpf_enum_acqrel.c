/* Linux 6.6 kernel/bpf/bpf_struct_ops.c uses an enum-valued state field
 * with smp_load_acquire/smp_store_release. GNU binds enum as a 32-bit GPR
 * and `Q` as exactly one hardware address, not a compiler fence. */
enum minic_bpf_state {
    MINIC_BPF_STATE_INIT,
    MINIC_BPF_STATE_INUSE,
    MINIC_BPF_STATE_TOBEFREE,
    MINIC_BPF_STATE_READY
};
struct minic_bpf_kvalue { enum minic_bpf_state state; };

enum minic_bpf_state minic_bpf_load(struct minic_bpf_kvalue *value) {
    enum minic_bpf_state result;
    asm volatile("ldar %w0, %1" : "=r" (result) : "Q" (value->state) : "memory");
    return result;
}

void minic_bpf_publish(struct minic_bpf_kvalue *value,
                       enum minic_bpf_state state) {
    asm volatile("stlr %w1, %0" : "=Q" (value->state) : "rZ" (state) : "memory");
}

#ifdef MINIC_BAD_LARGE_ENUM
/* This enum's effective type is 64-bit; 32-bit LDAR/STLR must reject it. */
enum minic_bad_wide { MINIC_BAD_WIDE = 0x100000000ULL };
enum minic_bad_wide minic_bad_load(enum minic_bad_wide *ptr) {
    enum minic_bad_wide result;
    asm volatile("ldar %w0, %1" : "=r" (result) : "Q" (*ptr) : "memory");
    return result;
}
#endif
