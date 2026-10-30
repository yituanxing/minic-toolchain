/* Fixed Linux v6.6.143 net/netfilter/nf_conntrack_extend.c shape.
 * Reduced historical-owner candidate, NOT authenticated original frozen .i. */
typedef unsigned int u32;
enum { NF_CT_EXT_NUM = 5 };
struct nf_ct_ext { u32 offset[8]; u32 len; };
struct nf_conn_help { unsigned char data[24]; };
struct nf_conn_nat { unsigned char data[16]; };
struct nf_conn_seqadj { unsigned char data[12]; };
struct nf_conn_acct { unsigned char data[24]; };
struct nf_conntrack_ecache { unsigned char data[20]; };
struct nf_conn_tstamp { unsigned char data[16]; };

extern void __compiletime_assert_697(void);
extern void runtime_size_assert_probe(void);
static volatile u32 observable_loop_counter;

static inline __attribute__((__gnu_inline__)) __attribute__((__unused__))
    __attribute__((no_instrument_function)) __attribute__((__always_inline__))
    u32 total_extension_size(void)
{
    /* Upstream has this separate BUG_ON before the `sizeof` return. Its
     * do/while AST wrapper is significant to compile-time helper folding. */
    do {
        extern void __compiletime_assert_696(void);
        if (!(NF_CT_EXT_NUM <= 10))
            __compiletime_assert_696();
    } while (0);
    return sizeof(struct nf_ct_ext) + sizeof(struct nf_conn_help) +
           sizeof(struct nf_conn_nat) + sizeof(struct nf_conn_seqadj) +
           sizeof(struct nf_conn_acct) + sizeof(struct nf_conntrack_ecache) +
           sizeof(struct nf_conn_tstamp);
}

static u32 nf_ct_ext_add_size(void)
{
    if (total_extension_size() > 255U)
        __compiletime_assert_697();
    return total_extension_size();
}

/* The leading-loop optimization MUST reject an actual write.  The caller
 * observes this effect, so neither GNU nor MiniC may remove it while proving
 * the otherwise constant return. */
static __attribute__((always_inline)) inline u32 impure_size(void)
{
    do {
        ++observable_loop_counter;
    } while (0);
    return total_extension_size();
}

int main(void)
{
    return nf_ct_ext_add_size() == 148U &&
           impure_size() == 148U &&
           observable_loop_counter == 1U ? 42 : 1;
}

#ifdef MINIC_NEGATIVE_CASES
u32 nf_ct_dynamic_guard(u32 externally_supplied)
{
    if (total_extension_size() + externally_supplied > 255U)
        runtime_size_assert_probe();
    return externally_supplied;
}

u32 nf_ct_inverted_guard(u32 incoming)
{
    if (incoming > total_extension_size())
        runtime_size_assert_probe();
    return incoming;
}
#endif
