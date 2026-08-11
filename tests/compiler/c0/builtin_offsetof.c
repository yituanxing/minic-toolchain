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

int main(void) {
    char padding[__builtin_offsetof(Sample, tail)];

    return sizeof(padding) == 8 && __builtin_offsetof(Sample, value) == 4 &&
                   __builtin_offsetof(struct Sample, tail) == 8 &&
                   __builtin_offsetof(struct BranchData, hit) == 32 &&
                   __builtin_offsetof(struct BranchData, miss_hit) == 24
               ? 0
               : 1;
}
