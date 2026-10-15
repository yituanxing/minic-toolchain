/*
 * Regression from Linux mm/gup.c: sanity_check_pinned_pages().
 * A parameter initially specialized to a constant must be re-evaluated
 * after the loop's decrement and pointer advance.  The old MiniC profile
 * emitted an unconditional backedge and read past the last page.
 *
 * Keep the source executable by ordinary GCC and the MiniC RV64 oracle.
 */
struct page_probe {
    unsigned long marker;
};

static __attribute__((always_inline)) inline int
scan_pages(struct page_probe **pages, unsigned long npages)
{
    int sum = 0;
    for (; npages; npages--, pages++) {
        struct page_probe *page = *pages;
        if (page == 0)
            return -99;
        sum += (int)page->marker;
    }
    return sum;
}

int main(void)
{
    struct page_probe p0 = {7}, p1 = {13}, p2 = {19};
    struct page_probe *pages[4] = {&p0, &p1, &p2, (void *)0};
    if (scan_pages(pages, 0) != 0)
        return 1;
    if (scan_pages(pages, 1) != 7)
        return 2;
    if (scan_pages(pages, 2) != 20)
        return 3;
    if (scan_pages(pages, 3) != 39)
        return 4;
    if (scan_pages(pages + 2, 1) != 19)
        return 5;
    return 0;
}
