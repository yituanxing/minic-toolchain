struct Forward;
union Slot;

struct Forward *pass(struct Forward *p);

struct Forward {
    int value;
};

union Slot {
    int value;
    long wide;
};

struct Forward *pass(struct Forward *p) {
    return p;
}

int read_forward(struct Forward *p) {
    return pass(p)->value;
}
