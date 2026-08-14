enum Forward;
extern enum Forward forward_value(void);

enum Forward {
    FORWARD_ZERO = 0,
    FORWARD_ONE = 1,
};

_Static_assert(__builtin_types_compatible_p(enum Forward, unsigned int),
               "positive enum should be compatible with unsigned int");

enum SignedEnum {
    SIGNED_NEGATIVE = -1,
    SIGNED_ZERO = 0,
};
_Static_assert(__builtin_types_compatible_p(enum SignedEnum, int),
               "negative int-range enum should be compatible with int");

enum MmCidState {
    MM_CID_UNSET_TEST = -1U,
    MM_CID_LAZY_PUT_TEST = (1U << 31),
};
_Static_assert(__builtin_types_compatible_p(enum MmCidState, unsigned int),
               "Linux mm cid enum should be compatible with unsigned int");
_Static_assert(MM_CID_UNSET_TEST == 0xffffffffU, "-1U enumerator must retain unsigned bits");
_Static_assert(MM_CID_LAZY_PUT_TEST == 0x80000000U, "high-bit enumerator must retain unsigned bits");

enum WideEnum {
    WIDE_ABORT_MASK = (0xffffffffULL << 32),
};
_Static_assert(__builtin_types_compatible_p(enum WideEnum, unsigned long),
               "64-bit positive enum should be compatible with unsigned long on RV64");

enum MixedEnum {
    MIXED_NEGATIVE = -1,
    MIXED_HIGH = 0xffffffffU,
};
_Static_assert(__builtin_types_compatible_p(enum MixedEnum, long),
               "mixed negative/high-positive enum should use signed long on RV64");

_Static_assert(!__builtin_types_compatible_p(enum Forward, enum SignedEnum),
               "distinct enum identities must remain distinct");

enum Forward forward_value(void) {
    return FORWARD_ONE;
}

int mm_state_test(int cid) {
    return cid == MM_CID_UNSET_TEST || (cid & MM_CID_LAZY_PUT_TEST);
}

unsigned long wide_enum_test(void) {
    return WIDE_ABORT_MASK;
}
