typedef long (*indirect_variadic_fn)(long, ...);

extern long indirect_variadic_sink(long tag, ...);

int main(void)
{
    indirect_variadic_fn fn = indirect_variadic_sink;

    return (int)fn(7L, 11L, 13L, 17L, 19L, 23L, 29L, 31L, 37L, 41L);
}
