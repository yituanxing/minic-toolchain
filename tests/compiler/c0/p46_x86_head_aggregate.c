/* MC-076: original C source for GCC and MiniC head physical-address roles.
 * Move the compiled .head.text together with its .head.data and execute the
 * displaced copy: RIP-relative record accesses follow the displacement,
 * while link-VMA pointer->integer round-trips must be rebased at memory use.
 */
struct P46Pair {
    unsigned long lo;
    unsigned long hi;
};

struct P46Pair x86_p46_head_src
    __attribute__((section(".head.data"))) = {0x37195UL, 0xeca264UL};
struct P46Pair x86_p46_head_dst
    __attribute__((section(".head.data"))) = {0x11UL, 0x22UL};

__attribute__((section(".head.text"), noinline, used))
void x86_p46_head_copy_direct(void) {
    x86_p46_head_dst = x86_p46_head_src;
}

__attribute__((section(".head.text"), noinline, used))
void x86_p46_head_copy_cast(void) {
    *(struct P46Pair *)(unsigned long)&x86_p46_head_dst =
        *(struct P46Pair *)(unsigned long)&x86_p46_head_src;
}

__attribute__((section(".head.text"), noinline, used))
void x86_p46_head_copy_load(void) {
    struct P46Pair value = x86_p46_head_src;
    x86_p46_head_dst = value;
}
