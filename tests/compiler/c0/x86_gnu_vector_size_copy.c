/* Linux arch/x86/kvm/hyperv.c style 16-byte vector storage assignment.
 * This is storage only: no SSE arithmetic, value-return, or vector call ABI. */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u32 sse128_t __attribute__((vector_size(16)));

union kvm_xmm {
    sse128_t vec;
    u64 words[2];
};

struct kvm_state {
    unsigned char pad;
    union kvm_xmm xmm;
    unsigned int tail;
};

static union kvm_xmm source, destination;
static struct kvm_state nested;

struct hyperv_hc {
    sse128_t xmm[1];
};

static u64 hyperv_mask(struct hyperv_hc *hc) {
    /* Mirrors the blocking Linux arch/x86/kvm/hyperv.c expression shape. */
    return ({
        union kvm_xmm flush;
        flush.vec = hc->xmm[0];
        flush.words[0];
    });
}

int main(void) {
    struct hyperv_hc hyperv;
    source.words[0] = 0x0123456789abcdefULL;
    source.words[1] = 0xfedcba9876543210ULL;
    destination.vec = source.vec;
    if (destination.words[0] != source.words[0] ||
        destination.words[1] != source.words[1]) return 1;

    source.words[0] = 0x1122334455667788ULL;
    source.words[1] = 0x8877665544332211ULL;
    nested.xmm.vec = source.vec;
    if (nested.xmm.words[0] != source.words[0] ||
        nested.xmm.words[1] != source.words[1]) return 2;
    destination.vec = nested.xmm.vec;
    if (destination.words[0] != source.words[0] ||
        destination.words[1] != source.words[1]) return 3;
    hyperv.xmm[0] = source.vec;
    if (hyperv_mask(&hyperv) != source.words[0]) return 4;
    return 0;
}
