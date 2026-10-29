/* RECORD_LOAD must preserve each byte of a packed record even when its
 * nominal size matches a native word-width load/store.  The GCC packed
 * attribute makes each record align to one byte; a caller may pass an odd
 * address without violating that C type's alignment contract. */
struct __attribute__((packed)) packed_two { unsigned char data[2]; };
struct __attribute__((packed)) packed_four { unsigned char data[4]; };
struct __attribute__((packed)) packed_eight { unsigned char data[8]; };

_Static_assert(_Alignof(struct packed_two) == 1, "packed_two alignment");
_Static_assert(_Alignof(struct packed_four) == 1, "packed_four alignment");
_Static_assert(_Alignof(struct packed_eight) == 1, "packed_eight alignment");

struct packed_two load_packed_two(const struct packed_two *p) { return *p; }
struct packed_four load_packed_four(const struct packed_four *p) { return *p; }
struct packed_eight load_packed_eight(const struct packed_eight *p) { return *p; }

/* Exercise a load with an extra addressable one-byte local. The frame
 * planner, not source declaration order, chooses the snapshot offset. */
struct packed_four load_packed_four_with_local(const struct packed_four *p) {
    volatile unsigned char guard = p->data[0];
    (void)guard;
    return *p;
}

/* Natural aligned records use the same fallback because the Core load pointer
 * currently has no transferable proof of its actual runtime alignment. */
struct natural_two { unsigned short value; };
struct natural_four { unsigned int value; };
struct natural_eight { unsigned long long value; };

struct natural_two load_natural_two(const struct natural_two *p) { return *p; }
struct natural_four load_natural_four(const struct natural_four *p) { return *p; }
struct natural_eight load_natural_eight(const struct natural_eight *p) { return *p; }

/* The member's type is naturally four-aligned, but its address is not:
 * the containing packed record places it at byte offset one. */
struct __attribute__((packed)) packed_outer_four {
    unsigned char prefix;
    struct natural_four member;
};
struct natural_four load_natural_field_packed(const struct packed_outer_four *p) {
    return p->member;
}

/* The first one-byte by-value parameter may place the second packed record
 * at an odd frame offset. This exercises incoming register scatter, outgoing
 * register gather and return-value aggregate marshalling. */
struct packed_one { unsigned char data[1]; } __attribute__((packed));
struct packed_two mix_after_one_two(struct packed_one first, struct packed_two value) {
    value.data[0] = first.data[0];
    return value;
}
struct packed_four mix_after_one_four(struct packed_one first, struct packed_four value) {
    value.data[0] = first.data[0];
    return value;
}
struct packed_eight mix_after_one_eight(struct packed_one first, struct packed_eight value) {
    value.data[0] = first.data[0];
    return value;
}

struct packed_four call_after_one_four(struct packed_one first, struct packed_four value) {
    return mix_after_one_four(first, value);
}
