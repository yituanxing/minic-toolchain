/* Linux AArch64's no-PLT ELF link requires undefined weak direct calls to
 * branch through a GOT-resolved function pointer, even in -fno-pie mode.
 * Defined weak and ordinary strong functions retain direct BL semantics. */
extern int minic_missing_weak_call(int) __attribute__((weak));
extern int minic_present_weak_call(int) __attribute__((weak));
extern int minic_strong_call(int);

int minic_defined_weak_call(int) __attribute__((weak));
int minic_defined_weak_call(int value)
{
    return value * 2;
}

int minic_weak_direct_dispatch(int value)
{
    if (minic_missing_weak_call)
        return minic_missing_weak_call(value);
    if (minic_present_weak_call)
        return minic_present_weak_call(value);
    return minic_strong_call(value);
}

int main(void)
{
    if (minic_weak_direct_dispatch(7) != 17)
        return 1;
    if (minic_defined_weak_call(8) != 16)
        return 2;
    return 0;
}
