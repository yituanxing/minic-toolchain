/* Linux v6.6.143 mm/memory.c: zap_pte_range's mutable delay_rmap is 0/1.
 * The inline TLB helper forwards that parameter to encode_page's
 * BUILD_BUG_ON(flags > ENCODE_PAGE_BITS). This fixture is a separate source
 * regression; the frozen 3.1 MiB original .i is replayed independently. */
struct page { unsigned long value; };
struct encoded_page;

#if defined(MINIC_NEGATIVE_CASE)
extern void __compiletime_assert_161(void);
#else
extern void __compiletime_assert_161(void)
    __attribute__((error("BUILD_BUG_ON failed: flags > 3")));
#endif

static inline __attribute__((always_inline)) struct encoded_page *
encode_page(struct page *page, unsigned long flags) {
    if (!(!(flags > 3ul))) __compiletime_assert_161();
    return (struct encoded_page *)(flags | (unsigned long)page);
}

static inline __attribute__((always_inline)) unsigned long
remove_page_with_flags(struct page *page, unsigned int flags) {
    return (unsigned long)encode_page(page, flags);
}

#if !defined(MINIC_NEGATIVE_CASE)
static volatile int ticks;
static int check_condition(int value) {
    ++ticks;
    return value;
}

static unsigned long remove_in_caller(struct page *page, int select) {
    unsigned int delay_rmap = 0;
    if (check_condition(select)) delay_rmap = 1;
    return remove_page_with_flags(page, delay_rmap);
}

int main(void) {
    struct page page = {0};
    unsigned long raw = (unsigned long)&page;
    if ((raw & 3ul) != 0ul) return 12;
    if (remove_in_caller(&page, 0) != raw) return 13;
    if (remove_in_caller(&page, 1) != (raw | 1ul)) return 14;
    if (ticks != 2) return 15; /* The runtime condition still executes. */
    return 42;
}
#elif MINIC_NEGATIVE_CASE == 1
/* The 4 case is reachable: a may-one mask of 4 must NOT prove <= 3. */
unsigned long unsafe_four(struct page *page, int select) {
    unsigned int flags = 0;
    if (select) flags = 4;
    return remove_page_with_flags(page, flags);
}
#elif MINIC_NEGATIVE_CASE == 2
/* An address escape invalidates all local facts. */
extern void mutate(unsigned int *flags);
unsigned long unsafe_escape(struct page *page) {
    unsigned int flags = 0;
    mutate(&flags);
    return remove_page_with_flags(page, flags);
}
#elif MINIC_NEGATIVE_CASE == 3
/* A volatile read cannot be replaced by the syntactic write set. */
unsigned long unsafe_volatile(struct page *page, int select) {
    volatile unsigned int flags = 0;
    if (select) flags = 1;
    return remove_page_with_flags(page, flags);
}
#elif MINIC_NEGATIVE_CASE == 4
/* An arbitrary unsigned incoming value retains the runtime assertion. */
unsigned long unsafe_parameter(struct page *page, unsigned int flags) {
    return remove_page_with_flags(page, flags);
}
#endif
