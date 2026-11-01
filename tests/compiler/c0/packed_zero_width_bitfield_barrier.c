/* GNU unnamed zero-width bit-fields retain their *natural type boundary*
 * even when a record's packed attribute or pragma pack caps other members.
 * This boundary does not increase the record's own alignment requirement. */
struct __attribute__((packed)) PackedInt {
    char lead;
    unsigned int : 0;
    char tail;
};

#pragma pack(push, 1)
struct PragmaInt {
    char lead;
    unsigned int : 0;
    char tail;
};
#pragma pack(pop)

struct __attribute__((packed)) PackedShort {
    char lead;
    unsigned short : 0;
    char tail;
};

struct __attribute__((packed)) PackedLongLong {
    char lead;
    unsigned long long : 0;
    char tail;
};

struct FieldPackedInt {
    char lead;
    unsigned int __attribute__((packed)) : 0;
    char tail;
};

struct __attribute__((packed)) FieldAlignedInt {
    char lead;
    unsigned int __attribute__((aligned(8))) : 0;
    char tail;
};

struct __attribute__((packed)) PackedAcrossBits {
    char lead;
    unsigned int low : 3;
    unsigned int : 0;
    unsigned int high : 4;
    char tail;
};

struct __attribute__((packed)) PackedWithoutBarrier {
    char lead;
    unsigned int : 3;
    char tail;
};

struct OrdinaryInt {
    char lead;
    unsigned int : 0;
    char tail;
};

_Static_assert(__builtin_offsetof(struct PackedInt, tail) == 4 &&
                   sizeof(struct PackedInt) == 5 &&
                   _Alignof(struct PackedInt) == 1,
               "packed int:0 is an alignment fence, not record alignment");
_Static_assert(__builtin_offsetof(struct PragmaInt, tail) == 4 &&
                   sizeof(struct PragmaInt) == 5 &&
                   _Alignof(struct PragmaInt) == 1,
               "pragma pack(1) does not erase int:0 fence");
_Static_assert(__builtin_offsetof(struct PackedShort, tail) == 2 &&
                   sizeof(struct PackedShort) == 3,
               "short:0 uses its two-byte natural boundary");
_Static_assert(__builtin_offsetof(struct PackedLongLong, tail) == 8 &&
                   sizeof(struct PackedLongLong) == 9,
               "long long:0 uses its eight-byte natural boundary");
_Static_assert(__builtin_offsetof(struct FieldPackedInt, tail) == 4 &&
                   sizeof(struct FieldPackedInt) == 5 &&
                   _Alignof(struct FieldPackedInt) == 1,
               "field-level packed does not erase unnamed zero-width fence");
_Static_assert(__builtin_offsetof(struct FieldAlignedInt, tail) == 8 &&
                   sizeof(struct FieldAlignedInt) == 9 &&
                   _Alignof(struct FieldAlignedInt) == 1,
               "field-level aligned(8) widens fence without aligning record");
_Static_assert(__builtin_offsetof(struct PackedAcrossBits, tail) == 5 &&
                   sizeof(struct PackedAcrossBits) == 6,
               "zero-width fence after partial packed storage unit");
_Static_assert(__builtin_offsetof(struct PackedWithoutBarrier, tail) == 2 &&
                   sizeof(struct PackedWithoutBarrier) == 3,
               "nonzero unnamed bit-field does not act as a fence");
_Static_assert(__builtin_offsetof(struct OrdinaryInt, tail) == 4 &&
                   sizeof(struct OrdinaryInt) == 5,
               "ordinary int:0 retains its existing boundary");

struct PackedInt packed_global = {.lead = 1, .tail = 2};
struct PragmaInt pragma_global = {.lead = 3, .tail = 4};
struct PackedAcrossBits bits_global = {
    .lead = 5, .low = 3, .high = 12, .tail = 6};

int main(void) {
    if (packed_global.tail != 2 || pragma_global.tail != 4 ||
        bits_global.low != 3 || bits_global.high != 12 ||
        bits_global.tail != 6) {
        return 1;
    }
    return 42;
}
