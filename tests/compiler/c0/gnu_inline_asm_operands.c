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

static int futex_constraint_shape(int value, int *target) {
    int ret = 0;
    int previous = 0;

    __asm__ __volatile__("amoswap.w.aqrl %[ov],%z[op],%[u]"
                         : [r] "+r"(ret), [ov] "=&r"(previous), [u] "+m"(*target)
                         : [op] "Jr"(value)
                         : "memory");
    return previous + ret;
}

typedef struct RegisterOutputs {
    unsigned long first;
    unsigned long second;
    unsigned long third;
    unsigned long fourth;
    unsigned long fifth;
} RegisterOutputs;

static void register_member_outputs_like(RegisterOutputs *target) {
    __asm__ __volatile__("li %0, 1\n\t"
                         "li %1, 2\n\t"
                         "li %2, 3\n\t"
                         "li %3, 4\n\t"
                         "li %4, 5"
                         : "=r"(target->first),
                           "=r"(target->second),
                           "=r"(target->third),
                           "=r"(target->fourth),
                           "=r"(target->fifth));
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

static int memory_input_linux_shape(const int *value);

int main(void) {
    int previous;
    int swap_target = 21;
    int swapped;
    int memory_probe = 0;
    RegisterOutputs register_probe = {0};

    atomic_add_like(5, &global_counter);
    previous = atomic_fetch_add_like(3, &global_counter);
    swapped = futex_constraint_shape(9, &swap_target);
    memory_output_store_like(8, &memory_probe);
    register_member_outputs_like(&register_probe);
    return previous == 12 && global_counter.counter == 15 && swapped == 21 && swap_target == 9 &&
                   linux_target_constraint_shape(5) == 12 && clobber_reservation(4, 6) == 10 &&
                   memory_probe == 8 && memory_input_linux_shape(&memory_probe) == 8 &&
                   register_probe.first == 1UL && register_probe.second == 2UL &&
                   register_probe.third == 3UL && register_probe.fourth == 4UL &&
                   register_probe.fifth == 5UL
               ? 0
               : 1;
}

static int memory_input_linux_shape(const int *value) {
    long error = 0;
    int loaded;

    __asm__ __volatile__("lw %1, %2" : "+r"(error), "=&r"(loaded) : "m"(*value));
    return loaded + (int)error;
}
