typedef unsigned long long u64;

extern u64 __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies_64;
extern unsigned long volatile __attribute__((__aligned__((1 << 6)), __section__(".data..cacheline_aligned"))) jiffies;
u64 ordinary = 1;
u64 jiffies_64 = 0;
unsigned long volatile jiffies = 0;

int main(void)
{
    return ordinary == 1 ? 0 : 1;
}
