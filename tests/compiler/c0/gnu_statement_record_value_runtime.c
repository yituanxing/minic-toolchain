/* Independent native GCC/MiniC oracle for GNU statement record snapshots. */
typedef struct record_box { long *lock; } record_box_t;
typedef struct discarded_record { long value; } discarded_record_t;
typedef struct discarded_empty {} discarded_empty_t;
typedef struct discarded_holder { discarded_empty_t cookie; } discarded_holder_t;

extern record_box_t construct_box(long *lock);
extern void assign_box(record_box_t *target, long *lock);
extern void discard_record_lvalue(discarded_record_t *value);
extern void discard_zero_record_member(discarded_holder_t *holder);

static int lock_calls;

int try_lock(long *lock) {
    lock_calls++;
    if (*lock != 0) return 0;
    *lock = 1;
    return 1;
}

int main(void) {
    long available = 0, unavailable = 1;
    record_box_t first = construct_box(&available);
    record_box_t failed = construct_box(&unavailable);
    record_box_t assigned = { (void *)0 };
    discarded_record_t discarded = { 89 };
    discarded_holder_t empty;

    assign_box(&assigned, &unavailable);
    discard_record_lvalue(&discarded);
    discard_zero_record_member(&empty);
    return first.lock == &available && available == 1 &&
           failed.lock == (void *)0 && assigned.lock == &unavailable &&
           unavailable == 1 && discarded.value == 89 && lock_calls == 2 ? 0 : 1;
}
