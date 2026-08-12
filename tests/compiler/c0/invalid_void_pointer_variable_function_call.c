int sink(int (*callback)(unsigned int cpu))
{
    (void)callback;
    return 0;
}

int bridge(void *opaque)
{
    return sink(opaque);
}

int main(void)
{
    return 0;
}
