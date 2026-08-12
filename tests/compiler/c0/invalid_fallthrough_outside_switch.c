int invalid_fallthrough_outside_switch(void)
{
    __attribute__((__fallthrough__));
    return 0;
}
