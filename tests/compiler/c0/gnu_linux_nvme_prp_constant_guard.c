/* Linux 6.6.143 drivers/nvme/host/pci.i:53436-53441,55828.
 * The original GNU -E source has exactly two initialized unsigned locals;
 * BUILD_BUG_ON must reject a real runtime guard, not this constant five. */
#define NVME_PAGE_SIZE (1U << 12)
#define NVME_MAX_KB_SZ 8192U
#define NVME_MAX_NR_ALLOCATIONS 5U
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1U) / (d))

static inline __attribute__((always_inline)) unsigned nvme_pci_npages_prp(void)
{
    unsigned max_bytes = (NVME_MAX_KB_SZ * 1024U) + NVME_PAGE_SIZE;
    unsigned nprps = DIV_ROUND_UP(max_bytes, NVME_PAGE_SIZE);
    return DIV_ROUND_UP(8U * nprps, NVME_PAGE_SIZE - 8U);
}

extern void __compiletime_assert_449(void)
    __attribute__((error("BUILD_BUG_ON failed: nvme_pci_npages_prp() > 5")));

int main(void)
{
    if (nvme_pci_npages_prp() > NVME_MAX_NR_ALLOCATIONS)
        __compiletime_assert_449();
    return 42;
}

#ifdef MINIC_NVME_PRP_NEGATIVES
extern volatile unsigned live_nprps;
extern void witnessed_effect(void);
extern void __compiletime_assert_runtime(void);

static inline __attribute__((always_inline)) unsigned effectful_prp(void)
{
    unsigned max_bytes = (NVME_MAX_KB_SZ * 1024U) + NVME_PAGE_SIZE;
    witnessed_effect();
    unsigned nprps = DIV_ROUND_UP(max_bytes, NVME_PAGE_SIZE);
    return DIV_ROUND_UP(8U * nprps, NVME_PAGE_SIZE - 8U);
}

unsigned negative_effect(void)
{
    return effectful_prp();
}

static inline __attribute__((always_inline)) unsigned volatile_prp(void)
{
    unsigned nprps = live_nprps;
    return DIV_ROUND_UP(8U * nprps, NVME_PAGE_SIZE - 8U);
}

void negative_volatile(void)
{
    if (volatile_prp() > NVME_MAX_NR_ALLOCATIONS)
        __compiletime_assert_runtime();
}

static inline __attribute__((always_inline)) unsigned parameter_prp(unsigned nprps)
{
    return DIV_ROUND_UP(8U * nprps, NVME_PAGE_SIZE - 8U);
}

void negative_argument(unsigned nprps)
{
    if (parameter_prp(nprps) > NVME_MAX_NR_ALLOCATIONS)
        __compiletime_assert_runtime();
}
#endif
