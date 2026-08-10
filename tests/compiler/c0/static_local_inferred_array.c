typedef unsigned char MiniByte;

typedef struct MiniRegs {
    unsigned long pad;
    unsigned int a0;
    unsigned int a1;
} MiniRegs;

typedef struct MiniPair {
    int first;
    int second;
} MiniPair;

int static_table_checksum(void) {
    static const MiniByte nextage[] = {1, 3, 3, 4, 4, 5, 6};
    static const unsigned int argument_offs[] = {
        __builtin_offsetof(MiniRegs, a0),
        __builtin_offsetof(MiniRegs, a1),
        4 + 12,
    };
    static const int scalar_offset = __builtin_offsetof(MiniRegs, a1);
    static const MiniPair pair = {
        __builtin_offsetof(MiniRegs, a0),
        1 + 2,
    };

    return (int)sizeof(nextage) + nextage[0] + nextage[6] +
           (int)argument_offs[0] + (int)argument_offs[1] + argument_offs[2] + scalar_offset +
           pair.first + pair.second;
}
