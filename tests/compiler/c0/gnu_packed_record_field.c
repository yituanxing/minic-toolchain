struct PackedMiddle {
    unsigned char lead;
    unsigned long value __attribute__((__packed__));
    unsigned char tail;
};

struct PackedThenNatural {
    unsigned char lead;
    unsigned long packed_value __attribute__((packed));
    unsigned long natural_value;
};

struct PackedAligned {
    unsigned char lead;
    unsigned long value __attribute__((packed, aligned(4)));
    unsigned char tail;
};

_Static_assert(__builtin_offsetof(struct PackedMiddle, lead) == 0, "lead");
_Static_assert(__builtin_offsetof(struct PackedMiddle, value) == 1, "packed field offset");
_Static_assert(__builtin_offsetof(struct PackedMiddle, tail) == 9, "tail after packed field");
_Static_assert(sizeof(struct PackedMiddle) == 10, "packed field does not pack entire record");

_Static_assert(__builtin_offsetof(struct PackedThenNatural, packed_value) == 1, "packed value");
_Static_assert(__builtin_offsetof(struct PackedThenNatural, natural_value) == 16, "next field natural alignment");
_Static_assert(sizeof(struct PackedThenNatural) == 24, "record keeps natural alignment from normal field");

_Static_assert(__builtin_offsetof(struct PackedAligned, value) == 4, "aligned raises packed field alignment");
_Static_assert(__builtin_offsetof(struct PackedAligned, tail) == 12, "aligned packed field size");
_Static_assert(sizeof(struct PackedAligned) == 16, "explicit field alignment contributes to record");

int main(void)
{
    struct PackedMiddle value = {1, 2, 3};
    return value.lead == 1 && value.value == 2 && value.tail == 3 ? 0 : 1;
}
