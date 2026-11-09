/* Linux i915 intel_engine_cs.c uses [ENGINE_CLASS].reg = (i915_reg_t){...}. */
typedef struct MinicReg { unsigned reg; } MinicReg;
typedef union MinicUnionReg { MinicReg reg; MinicReg mcr_reg; } MinicUnionReg;

static const MinicUnionReg gen8_regs[4] = {
    [0].reg = (MinicReg){.reg = 0x4260U},
    [1].reg = (MinicReg){.reg = 0x4264U},
    [2].reg = (MinicReg){.reg = 0x4270U},
    [3].reg = (MinicReg){.reg = 0x426cU},
};
static const MinicUnionReg gen12_regs[6] = {
    [0].mcr_reg = (MinicReg){.reg = 0xced8U},
    [1].mcr_reg = (MinicReg){.reg = 0xcedcU},
    [2].mcr_reg = (MinicReg){.reg = 0xcee0U},
    [3].mcr_reg = (MinicReg){.reg = 0xcee4U},
    [5].mcr_reg = (MinicReg){.reg = 0xcf04U},
};
struct MinicPair { unsigned lo; unsigned hi; };
static const struct MinicPair pairs[3] = {
    [0].lo = 1U,
    [0].hi = 2U,
    [2].hi = 3U,
    [2].lo = 4U,
};
struct MinicOuter { unsigned tag; struct MinicPair inner; };
static const struct MinicOuter nested[2] = {
    [0].inner.lo = 10U,
    [0].inner.hi = 20U,
    [1].tag = 7U,
    [1].inner.hi = 30U,
};

int main(void) {
    if (sizeof(gen8_regs)/sizeof(gen8_regs[0]) != 4U ||
        sizeof(gen12_regs)/sizeof(gen12_regs[0]) != 6U ||
        sizeof(pairs)/sizeof(pairs[0]) != 3U) return 1;
    if (gen8_regs[0].reg.reg != 0x4260U ||
        gen8_regs[3].reg.reg != 0x426cU ||
        gen12_regs[5].mcr_reg.reg != 0xcf04U ||
        gen12_regs[4].reg.reg != 0U) return 2;
    return pairs[0].lo == 1U && pairs[0].hi == 2U &&
           pairs[1].lo == 0U && pairs[1].hi == 0U &&
           pairs[2].lo == 4U && pairs[2].hi == 3U &&
           nested[0].tag == 0U && nested[0].inner.lo == 10U &&
           nested[0].inner.hi == 20U && nested[1].tag == 7U &&
           nested[1].inner.lo == 0U && nested[1].inner.hi == 30U ? 0 : 3;
}
