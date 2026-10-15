struct target_node {
    int value;
};

union shape_union {
    struct {
        unsigned char a;
        unsigned char b;
        unsigned char c;
        unsigned char d;
    } bytes;
    unsigned int word;
};

struct holder {
    union shape_union shape;
    int after;
    struct target_node *next;
};

struct target_node target = { .value = 9 };
struct holder sample = {
    .after = 7,
    .shape.word = 0,
    .next = &target,
};

int union_shape_overlay_probe(void) {
    return sample.shape.word == 0 && sample.after == 7 && sample.next == &target;
}
