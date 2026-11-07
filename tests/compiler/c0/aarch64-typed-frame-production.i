typedef unsigned long u64;
static volatile u64 memory;
static volatile double numbers;

__attribute__((noinline)) static double floating(double a, float f) {
    double b = a * (double)f;
    double c = b / 2.0 + a;
    numbers = c;
    return c;
}

__attribute__((noinline)) static u64 effect(u64 *address) {
    u64 x = *address;
    *address = x + 1UL;
    return *address;
}

int main(void) {
    u64 local = 41UL;
    memory = effect(&local);
    return memory == 42UL && floating(4.0, 2.0f) == 8.0 ? 42 : 1;
}
