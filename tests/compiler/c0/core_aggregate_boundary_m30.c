typedef unsigned long core_m30_u64;
typedef unsigned int core_m30_u32;

struct CoreM30Pair {
    long a;
    long b;
};

struct CoreM30One {
    long a;
};

static void core_m30_set_pair(struct CoreM30Pair *p, long a, long b) {
    p->a = a;
    p->b = b;
}

struct CoreM30Pair core_m30_pair_add(struct CoreM30Pair lhs, struct CoreM30Pair rhs) {
    struct CoreM30Pair delta;

    core_m30_set_pair(&delta, lhs.a + rhs.a, lhs.b + rhs.b);
    return delta;
}

struct CoreM30Pair core_m30_return_param(struct CoreM30Pair value) {
    return value;
}

long core_m30_after_record(struct CoreM30Pair value, long tail) {
    return value.a + value.b + tail;
}

long core_m30_scalar_record_scalar(long head, struct CoreM30Pair value, long tail) {
    return head + value.a + value.b + tail;
}

long core_m30_one_chunk(struct CoreM30One value, long tail) {
    return value.a + tail;
}

static core_m30_u32 core_m30_iter_div(core_m30_u64 dividend,
                                      core_m30_u32 divisor,
                                      core_m30_u64 *remainder) {
    core_m30_u32 ret = 0U;

    while (dividend >= divisor) {
        __asm__("" : "+rm"(dividend));
        dividend -= divisor;
        ret++;
    }
    *remainder = dividend;
    return ret;
}

void core_m30_add_ns(struct CoreM30Pair *value, core_m30_u64 ns) {
    value->a += core_m30_iter_div((core_m30_u64)value->b + ns, 1000000000U, &ns);
    value->b = (long)ns;
}
