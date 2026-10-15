typedef void opaque_t;

static opaque_t *identity(opaque_t *value)
{
    return value;
}

int main(void)
{
    opaque_t *value = (opaque_t *)0;
    return identity(value) == (opaque_t *)0 ? 0 : 1;
}
