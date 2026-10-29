struct align_tail {
    long head;
    unsigned char tail[];
};

/* A record's pragma pack limit and explicit field alignment participate in
 * __alignof__(member), independently of the field's natural C type size.
 * These source oracles must agree with GNU C and the canonical layout walk. */
#pragma pack(push, 2)
struct align_pack_two {
    char head;
    long long value;
};
#pragma pack(pop)

struct __attribute__((packed)) align_packed_override {
    char head;
    long long value __attribute__((aligned(2)));
};

struct align_field_override {
    char head;
    long long value __attribute__((packed, aligned(4)));
};

/* Integer bitfields share the same effective alignment during allocation;
 * GNU C does not permit querying __alignof__(a_bit_field) directly. */
struct __attribute__((packed)) align_bits {
    unsigned int first : 3;
    unsigned int second : 5;
    unsigned char tail;
};

_Static_assert(sizeof(struct align_pack_two) == 10, "pragma-pack offsets");
_Static_assert(sizeof(struct align_packed_override) == 10, "packed+aligned offsets");
_Static_assert(sizeof(struct align_field_override) == 12, "field packed+aligned offsets");
_Static_assert(sizeof(struct align_bits) == 2, "packed bitfield layout");

unsigned long alignof_scalar_member(struct align_tail *ptr) {
    return __alignof__(ptr->head);
}

unsigned long alignof_flexible_array_member(struct align_tail *ptr) {
    return __alignof__(ptr->tail);
}

unsigned long alignof_pack_two_member(struct align_pack_two *ptr) {
    return __alignof__(ptr->value);
}

unsigned long alignof_packed_override_member(struct align_packed_override *ptr) {
    return __alignof__(ptr->value);
}

unsigned long alignof_field_override_member(struct align_field_override *ptr) {
    return __alignof__(ptr->value);
}

int main(void) {
    struct align_tail *ptr = 0;
    return (int)(alignof_scalar_member(ptr) != 8 ||
                 alignof_flexible_array_member(ptr) != 1 ||
                 alignof_pack_two_member(0) != 2 ||
                 alignof_packed_override_member(0) != 2 ||
                 alignof_field_override_member(0) != 4);
}
