/* GNU caller -> MiniC aggregate parameter and MiniC -> MiniC aggregate
 * parameter. No kernel or privileged operation is involved in this test. */
struct linux_membuf {
    void *p;
    unsigned long left;
};

extern unsigned long minic_linux_membuf_sum(struct linux_membuf);
extern unsigned long minic_linux_membuf_call(void *, unsigned long);

struct linux_timespec64 {
    long long tv_sec;
    long tv_nsec;
};
extern struct linux_timespec64 minic_linux_timespec_return(long long, long);
extern long long minic_linux_timespec_sum(struct linux_timespec64);
extern long long minic_linux_timespec_roundtrip(long long, long);

int main(void) {
    struct linux_membuf buffer;
    buffer.p = (void *)0x12340000UL;
    buffer.left = 0x5678UL;
    if (minic_linux_membuf_sum(buffer) != 0x12345678UL) return 1;
    if (minic_linux_membuf_call(buffer.p, buffer.left) != 0x12345678UL)
        return 2;
    {
        struct linux_timespec64 ts =
            minic_linux_timespec_return(-1000, 987654321);
        if (ts.tv_sec != -1000 || ts.tv_nsec != 987654321) return 3;
        if (minic_linux_timespec_sum(ts) != 987653321) return 4;
        if (minic_linux_timespec_roundtrip(-400, 890) != 490) return 5;
    }
    return 0;
}
