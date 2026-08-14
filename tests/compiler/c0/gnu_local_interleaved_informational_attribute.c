typedef long long s64;

int main(void) {
    s64 __attribute__((__unused__)) steal = 0, irq_delta = 0;
    return steal == 0 && irq_delta == 0 ? 0 : 1;
}
