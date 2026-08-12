struct Sample {
    char first;
    int value;
    unsigned short tail;
};

typedef struct Sample Sample;

struct BranchData {
    const char *func;
    const char *file;
    unsigned line;
    union {
        struct {
            unsigned long correct;
            unsigned long incorrect;
        };
        struct {
            unsigned long miss;
            unsigned long hit;
        };
        unsigned long miss_hit[2];
    };
};

_Static_assert(__builtin_offsetof(struct BranchData, hit) == 32,
               "promoted anonymous member offsetof");
_Static_assert(__builtin_offsetof(struct BranchData, miss_hit) == 24,
               "promoted anonymous array member offsetof");

struct IndexedOffset {
    char lead;
    unsigned long node[2];
};

_Static_assert(__builtin_offsetof(struct IndexedOffset, node[1]) == 16,
               "constant indexed offsetof");

unsigned long indexed_offset(unsigned int idx) {
    return __builtin_offsetof(struct IndexedOffset, node[idx]);
}

int main(void) {
    char padding[__builtin_offsetof(Sample, tail)];

    return sizeof(padding) == 8 && __builtin_offsetof(Sample, value) == 4 &&
                   __builtin_offsetof(struct Sample, tail) == 8 &&
                   __builtin_offsetof(struct BranchData, hit) == 32 &&
                   __builtin_offsetof(struct BranchData, miss_hit) == 24 &&
                   indexed_offset(0) == 8 && indexed_offset(1) == 16
               ? 0
               : 1;
}

struct Flow4 {
    unsigned short family;
    unsigned int mark;
};

struct Flowi {
    char lead;
    union {
        struct Flow4 ip4;
        unsigned long raw;
    } u;
};

_Static_assert(__builtin_offsetof(struct Flowi, u.ip4) == 8, "nested member offsetof Linux shape");
_Static_assert(__builtin_offsetof(struct Flowi, u.ip4.mark) == 12,
               "nested member offsetof accumulates record offsets");

struct NestedElement {
    char lead;
    unsigned int value;
};

struct NestedGrid {
    char lead;
    struct NestedElement rows[3];
};

_Static_assert(__builtin_offsetof(struct NestedGrid, rows[2].value) == 24,
               "array then nested member offsetof");

unsigned long nested_indexed_offset(unsigned int idx) {
    return __builtin_offsetof(struct NestedGrid, rows[idx].value);
}

struct MatrixOffset {
    char lead;
    unsigned short cell[2][3];
};

_Static_assert(__builtin_offsetof(struct MatrixOffset, cell[1][2]) == 12,
               "multidimensional offsetof designator");
