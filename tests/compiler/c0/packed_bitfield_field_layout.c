/* GNU field attributes are layout facts even on a named bit-field. */
struct PackedHead {
    char lead;
    __attribute__((packed)) unsigned int bits : 3;
    char tail;
};

struct PackedTrailing {
    char lead;
    unsigned int bits : 3 __attribute__((packed));
    char tail;
};

struct NormalField {
    char lead;
    unsigned int bits : 3;
    char tail;
};

struct AlignedPackedField {
    char lead;
    __attribute__((packed, aligned(2))) unsigned int bits : 3;
    char tail;
};

struct PackedMulti {
    char lead;
    __attribute__((packed)) unsigned int first : 3, second : 5;
    char tail;
};

union PackedUnion {
    __attribute__((packed)) unsigned int bits : 3;
    char byte;
};

struct PackedUnnamed {
    char lead;
    __attribute__((packed)) unsigned int : 3;
    char tail;
};

_Static_assert(sizeof(struct PackedHead) == 3 &&
                   _Alignof(struct PackedHead) == 1,
               "declaration-head packed bit-field layout");
_Static_assert(sizeof(struct PackedTrailing) == 3 &&
                   _Alignof(struct PackedTrailing) == 1,
               "post-width packed bit-field layout");
_Static_assert(sizeof(struct NormalField) == 4 &&
                   _Alignof(struct NormalField) == 4,
               "ordinary named bit-field retains ABI alignment");
_Static_assert(sizeof(struct AlignedPackedField) == 4 &&
                   _Alignof(struct AlignedPackedField) == 2,
               "field aligned(2) overrides packed minimum");
_Static_assert(sizeof(struct PackedMulti) == 3 &&
                   _Alignof(struct PackedMulti) == 1,
               "declaration-head attributes apply to comma bit-fields");
_Static_assert(sizeof(union PackedUnion) == 1 &&
                   _Alignof(union PackedUnion) == 1,
               "union bit-field carries packed storage semantics");
_Static_assert(sizeof(struct PackedUnnamed) == 3 &&
                   _Alignof(struct PackedUnnamed) == 1,
               "unnamed bit-field retains GNU declaration attributes");

int main(void) {
    struct PackedHead h = {1, 5, 3};
    struct PackedTrailing t = {1, 6, 3};
    struct AlignedPackedField a = {1, 7, 3};
    struct PackedMulti m = {1, 3, 17, 5};
    union PackedUnion u = {.bits = 4};
    return h.bits == 5 && t.bits == 6 && a.bits == 7 &&
           m.first == 3 && m.second == 17 && u.bits == 4 ? 42 : 1;
}
