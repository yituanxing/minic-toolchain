struct Sample {
    char first;
    int value;
    unsigned short tail;
};

typedef struct Sample Sample;

int main(void) {
    char padding[__builtin_offsetof(Sample, tail)];

    return sizeof(padding) == 8 && __builtin_offsetof(Sample, value) == 4 &&
                   __builtin_offsetof(struct Sample, tail) == 8
               ? 0
               : 1;
}
