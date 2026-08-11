typedef struct record_box {
    long *lock;
} record_box_t;

extern int try_lock(long *lock);

record_box_t construct_box(long *lock)
{
    record_box_t box = ({
        record_box_t temporary = { .lock = lock }, *current = &temporary;
        if (current->lock && !try_lock(current->lock))
            current->lock = (void *)0;
        temporary;
    });
    return box;
}

void assign_box(record_box_t *target, long *lock)
{
    *target = ({
        record_box_t temporary = { .lock = lock };
        temporary;
    });
}
