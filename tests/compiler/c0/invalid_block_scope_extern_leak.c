unsigned long declaring_function(void)
{
    extern unsigned long hidden;
    return hidden;
}

unsigned long outside_scope(void)
{
    return hidden;
}
