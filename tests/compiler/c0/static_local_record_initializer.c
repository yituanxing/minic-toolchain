typedef enum MiniKind {
    MINI_KIND_ZERO = 0,
    MINI_KIND_VALUE = 7
} MiniKind;

typedef union MiniPayload {
    int integer;
    long wide;
} MiniPayload;

typedef struct MiniRecord {
    MiniKind kind;
    MiniPayload payload;
    int first;
    int second;
} MiniRecord;

int read_static_record(void) {
    static const MiniRecord value = {MINI_KIND_VALUE, {0}, (-1), (-2)};
    return value.kind + value.payload.integer + value.first + value.second;
}


typedef struct MiniAtomic {
    int counter;
} MiniAtomic;

typedef struct MiniStaticKey {
    MiniAtomic enabled;
    union {
        unsigned long type;
        void *entries;
    };
} MiniStaticKey;

typedef struct MiniStaticKeyTrue {
    MiniStaticKey key;
} MiniStaticKeyTrue;

int read_static_compound_record(void) {
    static MiniStaticKeyTrue once_key = (MiniStaticKeyTrue) {
        .key = { .enabled = { 1 }, { .type = 1UL } },
    };
    return once_key.key.enabled.counter + (int)once_key.key.type;
}
