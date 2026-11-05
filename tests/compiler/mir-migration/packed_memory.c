/* Unaligned accesses, volatile byte/word reads, bit fields and aggregate
 * copying.  Any selected path that drops a store or changes widths fails. */
struct __attribute__((packed)) MirPacked {
    unsigned char lead;
    unsigned long wide;
    unsigned short half;
    unsigned int flags : 5;
    unsigned int mode : 11;
    unsigned int high : 16;
    unsigned char tail[9];
};

volatile unsigned long mir_packed_sink;
volatile struct MirPacked mir_packed_global;

__attribute__((noinline))
unsigned long mir_packed_update(struct MirPacked *p,
                                unsigned long x) {
    unsigned long before = p->wide;
    p->wide = before ^ (x + 0x1234567UL);
    p->half = (unsigned short)x;
    p->mode = (unsigned int)(x >> 3);
    p->flags = (unsigned int)x & 31U;
    p->tail[3] = (unsigned char)(p->mode ^ p->flags);
    return p->wide + p->half + p->tail[3];
}

int main(void) {
    struct MirPacked copy = mir_packed_global;
    unsigned long result = mir_packed_update(&copy, 0xBAADF00DUL);
    mir_packed_global = copy;
    mir_packed_sink = result;
    return (int)(result & 255UL);
}
