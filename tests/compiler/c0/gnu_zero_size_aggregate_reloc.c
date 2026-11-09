/* GNU zero-sized aggregate members occupy no scalar relocation slots.
 * The following pointer relocations appear AFTER an array of zero-sized
 * records, and must retain their true GNU offsets without invented stride. */
struct Empty {};
struct Holder {
    struct Empty ignored[2];
    struct {
        long *ptr;
    } nested;
};

long gnu_zero_size_anchor = 81;
struct Holder gnu_zero_size_holders[2] = {
    { .nested = { .ptr = &gnu_zero_size_anchor } },
    { .nested = { .ptr = &gnu_zero_size_anchor } }
};

int main(void) {
    return gnu_zero_size_holders[0].nested.ptr == &gnu_zero_size_anchor &&
           gnu_zero_size_holders[1].nested.ptr == &gnu_zero_size_anchor &&
           *gnu_zero_size_holders[0].nested.ptr == 81 ? 0 : 1;
}
