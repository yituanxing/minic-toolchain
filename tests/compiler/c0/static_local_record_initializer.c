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
