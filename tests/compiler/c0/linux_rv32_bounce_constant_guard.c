/* Linux block/blk.h's CONFIG_BOUNCE=n constant-inline reachability shape. */
struct mini_queue { unsigned int bounce; unsigned int data; };
extern unsigned long max_low_pfn;
extern unsigned long max_pfn;
extern int __blk_queue_bounce(struct mini_queue *);
extern int bio_has_data(struct mini_queue *);

static inline int mini_queue_may_bounce(struct mini_queue *q) {
    return 0 && q->bounce == 1 && max_low_pfn >= max_pfn;
}

static inline int mini_queue_bounce(struct mini_queue *q) {
    if (__builtin_expect(mini_queue_may_bounce(q) && bio_has_data(q), 0))
        return __blk_queue_bounce(q);
    return 1;
}

int linux_rv32_bounce_const_guard(struct mini_queue *q) {
    return mini_queue_bounce(q);
}

#ifndef MINI_BOUNCE_TEST_DISABLED_ONLY
/* The unknown, effectful second operand MUST still run when reachable. */
int linux_rv32_bounce_enabled(struct mini_queue *q) {
    if (__builtin_expect(1 && bio_has_data(q), 0))
        return __blk_queue_bounce(q);
    return 1;
}

/* The first operand of && is always evaluated, even when the right is zero. */
int linux_rv32_bounce_effectful_left(struct mini_queue *q) {
    return bio_has_data(q) && 0;
}

/* The second operand of a true || must not be evaluated. */
int linux_rv32_bounce_true_or(struct mini_queue *q) {
    return 1 || bio_has_data(q);
}

/* A false || must execute its second operand. */
int linux_rv32_bounce_false_or(struct mini_queue *q) {
    return 0 || bio_has_data(q);
}

extern volatile unsigned int runtime_gate;
int linux_rv32_bounce_volatile_left(struct mini_queue *q) {
    return runtime_gate && 0 && bio_has_data(q);
}
#endif
