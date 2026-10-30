/* Provider is always compiled with GNU, never MiniC. */
int minic_present_weak_call(int value)
{
    return value + 10;
}

int minic_strong_call(int value)
{
    return value + 100;
}
