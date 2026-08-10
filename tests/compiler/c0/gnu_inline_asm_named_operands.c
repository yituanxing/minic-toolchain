typedef struct AtomicLike {
    int counter;
} AtomicLike;

static AtomicLike global_counter = {5};

static int fetch_add_unless_like(AtomicLike *v, int a, int u) {
    int prev;
    int rc;

    __asm__ __volatile__(
        "0:\tlr.w     %[p],  %[c]\n"
        "\tbeq      %[p],  %[u], 1f\n"
        "\tadd      %[rc], %[p], %[a]\n"
        "\tsc.w.rl  %[rc], %[rc], %[c]\n"
        "\tbnez     %[rc], 0b\n"
        "\tfence    rw, rw\n"
        "1:\n"
        : [p] "=&r"(prev), [rc] "=&r"(rc), [c] "+A"(v->counter)
        : [a] "r"(a), [u] "r"(u)
        : "memory");
    return prev;
}

int main(void) {
    int first;
    int second;

    first = fetch_add_unless_like(&global_counter, 3, 99);
    second = fetch_add_unless_like(&global_counter, 4, 8);
    return first == 5 && second == 8 && global_counter.counter == 8 ? 0 : 1;
}
