static int probe_calls;

static int probe(int value)
{
    probe_calls += 1;
    return value;
}

int linux_shape(int ret)
{
    return ret ?: -7;
}

int evaluate_once(void)
{
    return probe(5) ?: 9;
}

int false_fallback(void)
{
    return 0 ?: 9;
}

_Static_assert((5 ?: 9) == 5, "GNU omitted conditional true consteval");
_Static_assert((0 ?: 9) == 9, "GNU omitted conditional false consteval");

int main(void)
{
    return linux_shape(3) + false_fallback() + evaluate_once() + probe_calls == 18 ? 0 : 1;
}
