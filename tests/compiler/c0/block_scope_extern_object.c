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
