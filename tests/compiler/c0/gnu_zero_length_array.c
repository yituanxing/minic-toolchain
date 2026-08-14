typedef long atomic_long_t;
typedef int zero_ints[0];

_Static_assert(sizeof(zero_ints) == 0, "GNU zero-length array sizeof");

extern atomic_long_t vm_numa_event[];
extern atomic_long_t vm_numa_event[0];

int *decay_zero(zero_ints *holder)
{
    return *holder;
}

int main(void)
{
    return sizeof(zero_ints) == 0 ? 0 : 1;
}
