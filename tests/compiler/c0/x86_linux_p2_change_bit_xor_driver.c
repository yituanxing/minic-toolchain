#include <pthread.h>
#include <stddef.h>

extern void x86_p2_change_fixed(volatile unsigned long *addr);
extern void x86_p2_change_variable(long nr, volatile unsigned long *addr);

static volatile unsigned long word;

static void *change_fixed_concurrently(void *unused)
{
    size_t i;
    (void)unused;
    for (i = 0; i < 20001; ++i)
        x86_p2_change_fixed(&word);
    return NULL;
}

int main(void)
{
    pthread_t threads[6];
    size_t i;

    x86_p2_change_fixed(&word);
    if (word != (1UL << 9)) return 1;
    x86_p2_change_fixed(&word);
    if (word != 0UL) return 2;
    x86_p2_change_variable(31, &word);
    if (word != (1UL << 31)) return 3;
    x86_p2_change_variable(63, &word);
    if (word != ((1UL << 31) | (1UL << 63))) return 4;
    x86_p2_change_variable(31, &word);
    x86_p2_change_variable(63, &word);
    if (word != 0UL) return 5;
    x86_p2_change_variable(9, &word);
    x86_p2_change_fixed(&word);
    if (word != 0UL) return 6;

    /* Six odd-parity writers all toggle bit 9. If LOCK XOR were reduced to
     * ordinary non-atomic read/modify/write, lost toggles would be exposed. */
    for (i = 0; i < sizeof(threads) / sizeof(threads[0]); ++i)
        if (pthread_create(&threads[i], NULL, change_fixed_concurrently, NULL))
            return 7;
    for (i = 0; i < sizeof(threads) / sizeof(threads[0]); ++i)
        if (pthread_join(threads[i], NULL)) return 8;
    return word == 0UL ? 0 : 9;
}
