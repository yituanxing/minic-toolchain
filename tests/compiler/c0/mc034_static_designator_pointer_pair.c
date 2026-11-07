/* MC-034: designated static Record values must not subtract a snapshot
 * pointer from Program.records.  The phase shifts Record arena occupancy
 * without changing the observable static-object contract. */
#ifndef MC034_PREFIX_PHASE
#define MC034_PREFIX_PHASE 0
#endif

#if MC034_PREFIX_PHASE >= 1
struct mc034_pad_1 { long value; };
#endif
#if MC034_PREFIX_PHASE >= 2
struct mc034_pad_2 { char bytes[3]; };
#endif
#if MC034_PREFIX_PHASE >= 3
union mc034_pad_3 { int a; long b; };
#endif
#if MC034_PREFIX_PHASE >= 4
struct mc034_pad_4 { struct { short inner; }; long tail; };
#endif
#if MC034_PREFIX_PHASE >= 5
struct mc034_pad_5 { int a[7]; };
#endif

struct mc034_simple { int x; };
static struct mc034_simple mc034_q = { .x = 3 };

struct mc034_outer {
    int head;
    struct { int promoted; int other; };
    int tail;
};
static struct mc034_outer mc034_outer_value = {
    .head = 5, .promoted = 7, .other = 11, .tail = 13
};

union mc034_union {
    unsigned long bits;
    struct { unsigned long low; };
};
static union mc034_union mc034_union_value = { .low = 17UL };

int main(void) {
    return (mc034_q.x == 3 &&
            mc034_outer_value.head == 5 &&
            mc034_outer_value.promoted == 7 &&
            mc034_outer_value.other == 11 &&
            mc034_outer_value.tail == 13 &&
            mc034_union_value.low == 17UL) ? 0 : 1;
}
