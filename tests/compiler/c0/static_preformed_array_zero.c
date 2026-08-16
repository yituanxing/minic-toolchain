struct MiniMask {
    unsigned long bits[2];
};

typedef struct MiniMask MiniMaskVar[1];
typedef int MiniInts[4];
typedef const int MiniConstInts[2];

static MiniMaskVar plain_mask;
static __attribute__((section(".data..percpu"))) __typeof__(MiniMaskVar) percpu_mask;
static MiniInts integer_mask;
static MiniConstInts readonly_mask;

unsigned long read_preformed_arrays(void) {
    return plain_mask[0].bits[0] + percpu_mask[0].bits[1] + (unsigned long)integer_mask[3] +
           (unsigned long)readonly_mask[1];
}
