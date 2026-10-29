/* ARM64 Linux ghash-ce-glue.c: PMULL function pointer expects `const u64
 * h[][2]` but a writable flexible array member `u64 h[][2]` supplies it. */
typedef unsigned long long u64;

struct minic_ghash_key {
    u64 first;
    u64 h[][2];
};

typedef void (*minic_pmull_update)(int, u64 [], const char *,
                                   const u64 [][2], const char *);

static void minic_pmull_fallback(int blocks, u64 digest[], const char *src,
                                 const u64 h[][2], const char *head) {
    digest[0] += (u64)blocks + h[0][0];
    if (src != 0 && head != 0)
        digest[0] += 1U;
}

void minic_ghash_update(minic_pmull_update callback,
                        struct minic_ghash_key *key, u64 digest[]) {
    /* This is the real Linux `simd_update(..., key->h, head)` shape. */
    callback(1, digest, (const char *)0, key->h, (const char *)0);
    minic_pmull_fallback(1, digest, (const char *)0, key->h, (const char *)0);
}

static u64 minic_storage[2][2] = {{3U, 4U}, {5U, 6U}};

static u64 minic_sum_const(const u64 h[][2]) {
    return h[0][0] + h[1][0];
}

int main(void) {
    return minic_sum_const(minic_storage) == 8U ? 0 : 1;
}
