# 1 "pragma-pack.c"
#pragma pack(1)
struct packed_one {
    char lead;
    int value;
};
struct packed_two {
    char lead;
    unsigned long value;
};
#pragma pack()
struct natural_one {
    char lead;
    int value;
};
#pragma pack(2)
struct packed_align_two {
    char lead;
    int value;
};
#pragma pack(4)
struct packed_align_four {
    char lead;
    unsigned long value;
};
#pragma pack(8)
struct packed_align_eight {
    char lead;
    unsigned long value;
};
#pragma pack()
struct forward_record;
#pragma pack(1)
struct forward_record {
    char lead;
    int value;
};
#pragma pack()
struct attribute_packed {
    char lead;
    int value;
} __attribute__((packed));
_Static_assert(sizeof(struct packed_one) == 5, "pack(1) size");
_Static_assert(__builtin_offsetof(struct packed_one, value) == 1, "pack(1) offset");
_Static_assert(sizeof(struct packed_two) == 9, "pack state spans definitions");
_Static_assert(__builtin_offsetof(struct packed_two, value) == 1, "pack state offset");
_Static_assert(sizeof(struct natural_one) == 8, "pack reset size");
_Static_assert(__builtin_offsetof(struct natural_one, value) == 4, "pack reset offset");
_Static_assert(sizeof(struct packed_align_two) == 6, "pack(2) size");
_Static_assert(__builtin_offsetof(struct packed_align_two, value) == 2, "pack(2) offset");
_Static_assert(sizeof(struct packed_align_four) == 12, "pack(4) size");
_Static_assert(__builtin_offsetof(struct packed_align_four, value) == 4, "pack(4) offset");
_Static_assert(sizeof(struct packed_align_eight) == 16, "pack(8) size");
_Static_assert(__builtin_offsetof(struct packed_align_eight, value) == 8, "pack(8) offset");
_Static_assert(sizeof(struct forward_record) == 5, "definition-time pack state");
_Static_assert(__builtin_offsetof(struct forward_record, value) == 1, "forward identity");
_Static_assert(sizeof(struct attribute_packed) == 5, "GNU packed remains independent");
int main(void) { return 0; }
