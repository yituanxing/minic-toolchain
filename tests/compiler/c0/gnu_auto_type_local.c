static long linux_max_shape(long delta)
{
    return ({
        __auto_type x = (0L);
        __auto_type y = (delta);
        typeof(x) check = x + y;
        (void)check;
        x > y ? x : y;
    });
}

static long initializer_scope(long x)
{
    {
        __auto_type x = x + 1;
        return x;
    }
}

static int pointer_inference(void)
{
    int value = 0;
    __auto_type pointer = &value;
    *pointer = 7;
    return value;
}

int main(void)
{
    return linux_max_shape(-3) == 0 && linux_max_shape(9) == 9 &&
                   initializer_scope(4) == 5 && pointer_inference() == 7
               ? 0
               : 1;
}
