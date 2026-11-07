/* Intentionally header-free: host preprocessing is architecture neutral.
 * Exercises integer arithmetic, nested calls, outgoing stack arguments,
 * incoming stack arguments, locals, address-taken locals and frame layout. */
volatile long mir_sink;

__attribute__((noinline))
long mir_callee(long a, long b, long c, long d, long e,
                long f, long g, long h, long i, long j) {
    long mix = (a * 3L + b - c) ^ (d + e + f + g);
    return mix + h * 5L - i + j;
}

__attribute__((noinline))
long mir_frame(long seed) {
    long slots[12];
    long *volatile taken = slots;
    int i;
    for (i = 0; i < 12; ++i)
        slots[i] = seed + (long)i * 7L;
    mir_sink = slots[5];
    return mir_callee(slots[0], slots[1], slots[2], slots[3],
                      slots[4], slots[5], slots[6], slots[7],
                      slots[8], taken[9]);
}

int main(void) {
    return (int)(mir_frame(3L) & 255L);
}
