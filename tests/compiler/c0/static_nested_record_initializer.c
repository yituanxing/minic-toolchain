typedef union {
    void *pointer;
    long wide;
} MiniValue;

typedef struct {
    MiniValue value;
    unsigned char tag;
    int next;
    MiniValue tail;
} MiniEntry;

typedef union {
    MiniEntry entry;
    long wide;
} MiniNode;

static const MiniNode dummy = {
    {{(void *)0}, (1 << 4), 7, {(void *)0}}
};
