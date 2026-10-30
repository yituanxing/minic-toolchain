/* Linux 6.6.143 net/core/bpf_sk_storage.c:diag_get regression model.
 * The compile-time assertion is false. GNU min_t uses a statement
 * expression with two locally initialized temporaries, whose ownership is
 * inside the IF predicate and not its lexical predecessor. */
typedef unsigned int u32;

struct bpf_local_storage_elem {
    void *next;
    void *prev;
    void *sdata;
    unsigned int flags;
    unsigned int size;
};

#define U16_MAX 65535U
#define NLA_HDRLEN 4U
#define KMALLOC_MAX_SIZE (1U << 20)
#define MAX_BPF_STACK 512U
#define min_t(type, x, y) ({ type ux = (x); type uy = (y); ux < uy ? ux : uy; })
#define BPF_LOCAL_STORAGE_MAX_VALUE_SIZE \
    min_t(u32, (KMALLOC_MAX_SIZE - MAX_BPF_STACK - \
                sizeof(struct bpf_local_storage_elem)), \
               (U16_MAX - sizeof(struct bpf_local_storage_elem)))
#define BUILD_BUG_ON(c) do { extern void __compiletime_assert_645(void); \
    if (c) __compiletime_assert_645(); } while (0)

/* The nested owner test ensures the inner statement-expression block, not
 * merely the condition's first block, owns and initializes its locals. */
#define nested_min_t(type, x, y) ({ \
    type outer = (x); \
    type inner = ({ type ux = outer; type uy = (y); ux < uy ? ux : uy; }); \
    inner; })

int diag_get(int n) {
    BUILD_BUG_ON(U16_MAX - NLA_HDRLEN < BPF_LOCAL_STORAGE_MAX_VALUE_SIZE);
    return n + 1;
}

int diag_get_direct(int n) {
    BUILD_BUG_ON(U16_MAX - NLA_HDRLEN <
        (U16_MAX - sizeof(struct bpf_local_storage_elem)));
    return n + 2;
}

int diag_get_nested(int n) {
    BUILD_BUG_ON(U16_MAX - NLA_HDRLEN <
        nested_min_t(u32, (KMALLOC_MAX_SIZE - MAX_BPF_STACK -
                           sizeof(struct bpf_local_storage_elem)),
                           (U16_MAX - sizeof(struct bpf_local_storage_elem))));
    return n + 3;
}

/* Take the opposite min_t arm: the ownership proof must be independent of
 * which constant local actually becomes the result. */
int diag_get_small(int n) {
    BUILD_BUG_ON(U16_MAX - NLA_HDRLEN <
        min_t(u32, (4096U - MAX_BPF_STACK -
                    sizeof(struct bpf_local_storage_elem)),
                   (U16_MAX - sizeof(struct bpf_local_storage_elem))));
    return n + 4;
}
