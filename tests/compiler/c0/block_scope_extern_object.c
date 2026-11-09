unsigned long first(unsigned long pfn)
{
    extern unsigned long zero_pfn;
    extern unsigned long zero_pfn;
    return pfn == zero_pfn;
}

unsigned long second(void)
{
    extern unsigned long zero_pfn;
    return zero_pfn;
}

unsigned long before_promotion(void)
{
    extern unsigned long promoted;
    return promoted;
}

extern unsigned long promoted;

unsigned long after_promotion(void)
{
    return promoted;
}

/* Linux arch/loongarch/mm/cache.c declares a weak external object inside
 * a function. The GNU attribute belongs to the object symbol, not to a
 * function declaration or to the local block's storage. */
unsigned long weak_external_address(void)
{
    extern char __attribute__((__weak__)) except_vec_cex;
    return (unsigned long)&except_vec_cex;
}
