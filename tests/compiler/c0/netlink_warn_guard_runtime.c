/* Regress net/netlink/genetlink.c WARN_ON(!cnt) after split-op selection.
 * The warning must be guarded by a runtime branch: a bad MiniC core-lower
 * fact merge emits unconditional ebreak even when cnt becomes positive.
 * Structure intentionally matches the GNU statement-expression and
 * do-while(0) warning macro in Linux 6.6.143.
 */
struct netlink_probe_ops { unsigned flags; };
static struct netlink_probe_ops test_ops[] = { {2U}, {4U}, {6U} };

#define TEST_WARN_ON(x) ({                                        \
    int __ret_warn_on = !!(x);                                  \
    if (__builtin_expect(!!(__ret_warn_on), 0))                  \
        do { __asm__ __volatile__("ebreak"); } while (0);       \
    __builtin_expect(!!(__ret_warn_on), 0);                     \
})

static __attribute__((noinline)) int check_flags(unsigned index,
                                                  unsigned valid_limit)
{
    unsigned i = index;
    int cnt = 0;
    if (test_ops[i + (unsigned)cnt].flags & 2U) {
        cnt++;
    } else {
        __asm__ __volatile__("" ::: "memory");
    }
    if (i + (unsigned)cnt < valid_limit &&
        test_ops[i + (unsigned)cnt].flags & 4U) {
        cnt++;
    } else {
        __asm__ __volatile__("" ::: "memory");
    }
    TEST_WARN_ON(!cnt);
    return cnt;
}

int main(void)
{
    if (check_flags(0U, 3U) != 2) return 1;
    if (check_flags(1U, 3U) != 1) return 2;
    if (check_flags(2U, 3U) != 1) return 3;
    return 0;
}
