extern int minic_linux_bool_atomic_xchg(_Bool *value);

int main(void)
{
    _Bool value = 1;
    if (minic_linux_bool_atomic_xchg(&value) != 1 || value != 0)
        return 1;
    if (minic_linux_bool_atomic_xchg(&value) != 0 || value != 0)
        return 2;
    return 0;
}
