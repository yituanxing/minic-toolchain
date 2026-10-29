/* GNU caller -> MiniC aggregate parameter and MiniC -> MiniC aggregate
 * parameter. No kernel or privileged operation is involved in this test. */
struct linux_membuf {
    void *p;
    unsigned long left;
};

extern unsigned long minic_linux_membuf_sum(struct linux_membuf);
extern unsigned long minic_linux_membuf_call(void *, unsigned long);

int main(void) {
    struct linux_membuf buffer;
    buffer.p = (void *)0x12340000UL;
    buffer.left = 0x5678UL;
    if (minic_linux_membuf_sum(buffer) != 0x12345678UL) return 1;
    if (minic_linux_membuf_call(buffer.p, buffer.left) != 0x12345678UL)
        return 2;
    return 0;
}
