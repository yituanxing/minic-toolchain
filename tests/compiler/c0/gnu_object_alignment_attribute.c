typedef unsigned long long u64;

typedef struct {
    unsigned int __softirq_pending;
} irq_cpustat_t;

extern __attribute__((section(".data..percpu" "..shared_aligned")))
__typeof__(irq_cpustat_t) irq_stat __attribute__((__aligned__((1 << 6))));

extern __attribute__((section(".probe.suffix.aligned"))) __typeof__(u64) suffix_aligned
    __attribute__((__aligned__((1 << 6))));
extern u64 isolated_aligned __attribute__((__aligned__((1 << 6)))), isolated_natural;

extern u64 __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies_64;
extern unsigned long volatile __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies;
u64 ordinary = 1;
u64 suffix_aligned = 0;
u64 isolated_aligned = 0;
u64 isolated_natural = 0;
u64 jiffies_64 = 0;
unsigned long volatile jiffies = 0;

int main(void)
{
    return ordinary == 1 ? 0 : 1;
}
