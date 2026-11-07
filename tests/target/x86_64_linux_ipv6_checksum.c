/* Exact Linux csum_ipv6_magic GNU ADD/ADC form: two 128-bit addresses and
 * a tied scalar checksum accumulator. */
typedef unsigned long long u64;
struct minic_ipv6_words { u64 q[2]; };

#if defined(MINIC_IPV6_CSUM_NEGATIVE) && MINIC_IPV6_CSUM_NEGATIVE == 1
#define MINIC_IPV6_CSUM_DADC "\taddq (%[daddr]),%[sum]\n"
#else
#define MINIC_IPV6_CSUM_DADC "\tadcq (%[daddr]),%[sum]\n"
#endif

#if defined(MINIC_IPV6_CSUM_NEGATIVE) && MINIC_IPV6_CSUM_NEGATIVE == 2
#define MINIC_IPV6_CSUM_SADDR_CONSTRAINT "m"
#else
#define MINIC_IPV6_CSUM_SADDR_CONSTRAINT "r"
#endif

#if defined(MINIC_IPV6_CSUM_NEGATIVE) && MINIC_IPV6_CSUM_NEGATIVE == 3
#define MINIC_IPV6_CSUM_QUALIFIER volatile
#else
#define MINIC_IPV6_CSUM_QUALIFIER
#endif

#if defined(MINIC_IPV6_CSUM_NEGATIVE) && MINIC_IPV6_CSUM_NEGATIVE == 4
#define MINIC_IPV6_CSUM_TIE "r"
#else
#define MINIC_IPV6_CSUM_TIE "[sum]"
#endif

__attribute__((noinline)) u64 minic_ipv6_checksum(
    u64 rest, const struct minic_ipv6_words *saddr,
    const struct minic_ipv6_words *daddr) {
    u64 sum64;
    __asm__ MINIC_IPV6_CSUM_QUALIFIER(
        "\taddq (%[saddr]),%[sum]\n"
        "\tadcq 8(%[saddr]),%[sum]\n"
        MINIC_IPV6_CSUM_DADC
        "\tadcq 8(%[daddr]),%[sum]\n"
        "\tadcq $0,%[sum]\n"
        : [sum] "=r" (sum64)
        : MINIC_IPV6_CSUM_TIE (rest),
          [saddr] MINIC_IPV6_CSUM_SADDR_CONSTRAINT (saddr),
          [daddr] "r" (daddr));
    return sum64;
}
