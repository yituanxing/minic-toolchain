typedef struct record_box {
    long *lock;
} record_box_t;

extern int try_lock(long *lock);

record_box_t construct_box(long *lock) {
    record_box_t box = ({
        record_box_t temporary = {.lock = lock}, *current = &temporary;
        if (current->lock && !try_lock(current->lock))
            current->lock = (void *)0;
        temporary;
    });
    return box;
}

void assign_box(record_box_t *target, long *lock) {
    *target = ({
        record_box_t temporary = {.lock = lock};
        temporary;
    });
}

typedef struct discarded_record {
    long value;
} discarded_record_t;

typedef struct discarded_empty {
} discarded_empty_t;

typedef struct discarded_holder {
    discarded_empty_t cookie;
} discarded_holder_t;

static discarded_record_t *discard_record_source(discarded_record_t *value) {
    return value;
}

void discard_record_lvalue(discarded_record_t *value) {
    (void)(*discard_record_source(value));
}

void discard_zero_record_member(discarded_holder_t *holder) {
    (void)(holder->cookie);
}
