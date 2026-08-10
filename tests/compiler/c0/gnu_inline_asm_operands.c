typedef struct AtomicLike {
    int counter;
} AtomicLike;

static AtomicLike global_counter = {7};

static void atomic_add_like(int value, AtomicLike *target) {
    __asm__ __volatile__("amoadd.w zero, %1, %0"
                         : "+A"(target->counter)
                         : "r"(value)
                         : "memory");
}

static int atomic_fetch_add_like(int value, AtomicLike *target) {
    register int previous;

    __asm__ __volatile__("amoadd.w %1, %2, %0"
                         : "+A"(target->counter), "=r"(previous)
                         : "r"(value)
                         : "memory");
    return previous;
}

int main(void) {
    int previous;

    atomic_add_like(5, &global_counter);
    previous = atomic_fetch_add_like(3, &global_counter);
    return previous == 12 && global_counter.counter == 15 ? 0 : 1;
}
