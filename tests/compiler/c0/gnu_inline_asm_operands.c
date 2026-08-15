typedef struct AtomicLike {
    int counter;
} AtomicLike;

static AtomicLike global_counter = {7};

static void atomic_add_like(int value, AtomicLike *target) {
    __asm__ __volatile__("amoadd.w zero, %1, %0" : "+A"(target->counter) : "r"(value) : "memory");
}

static int atomic_fetch_add_like(int value, AtomicLike *target) {
    register int previous;

    __asm__ __volatile__("amoadd.w %1, %2, %0"
                         : "+A"(target->counter), "=r"(previous)
                         : "r"(value)
                         : "memory");
    return previous;
}

static void memory_output_store_like(int value, int *target) {
    __asm__ __volatile__("sw %z1, %0" : "=m"(*target) : "rJ"(value) : "memory");
}

static int linux_target_constraint_shape(int value) {
    __asm__ __volatile__("addi t3, zero, %1\n\t"
                         "add %0, %0, t3"
                         : "+r"(value)
                         : "I"(7)
                         : "t3");
    return value;
}

static int clobber_reservation(int left, int right) {
    int result;

    __asm__ __volatile__("add %0, %1, %2" : "=r"(result) : "r"(left), "r"(right) : "t3");
    return result;
}

int main(void) {
    int previous;

    atomic_add_like(5, &global_counter);
    previous = atomic_fetch_add_like(3, &global_counter);
    return previous == 12 && global_counter.counter == 15 &&
                   linux_target_constraint_shape(5) == 12 && clobber_reservation(4, 6) == 10
               ? 0
               : 1;
}
