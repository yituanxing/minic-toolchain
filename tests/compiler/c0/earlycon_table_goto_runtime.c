/* Linux 6.6 earlycon setup loop, reduced without including Linux headers.
 * Preserve the essential layout (152-byte descriptor), pointer induction,
 * continue edges, mutable _Bool retry, and backward goto on a for-exit.
 */
typedef _Bool bool;
typedef unsigned long size_t;
struct early_id {
    char name[16];
    char compatible[128];
    unsigned long payload;
};
static const struct early_id ids[3] = {
    {"uart", "vendor,uart", 1},
    {"sbi", "", 2},
    {"test", "vendor,test", 3}
};
static int iterations;
static size_t mini_len(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}
static int mini_cmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return 1;
    }
    return 0;
}
static int earlycon_table_probe(char *buf)
{
    const struct early_id *match;
    bool empty_compatible = 1;
    if (!buf || !buf[0])
        return -22;
again:
    for (match = ids; match < ids + 3; match++) {
        size_t len = mini_len(match->name);
        ++iterations;
        if (iterations > 12)
            return 99;
        if (mini_cmp(buf, match->name, len))
            continue;
        if (empty_compatible && *match->compatible)
            continue;
        if (buf[len]) {
            if (buf[len] != ',')
                continue;
            buf += len + 1;
        } else
            buf = 0;
        return (int)match->payload;
    }
    if (empty_compatible) {
        empty_compatible = 0;
        goto again;
    }
    return -2;
}
int earlycon_table_probe_entry(void)
{
    char no_match[] = "absent";
    char delayed[] = "uart";
    int result;
    iterations = 0;
    result = earlycon_table_probe(no_match);
    if (result != -2 || iterations != 6)
        return 11;
    iterations = 0;
    result = earlycon_table_probe(delayed);
    if (result != 1 || iterations != 4)
        return 12;
    return 0;
}
