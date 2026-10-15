int variadic_function_pointer_cast(void *opaque)
{
    return ((int (*)(int, int, ...))opaque)(11, 22, 33);
}
