struct refcount {
    long refs[1];
};
struct namespace_state {
    struct refcount count;
};
struct boot_state {
    struct namespace_state ns;
    char comm[8];
    long tail;
};

static struct boot_state state = {
    .ns.count = {.refs = {2}},
    .tail = 9,
    .comm = "swap",
};

int main(void) {
    if (state.ns.count.refs[0] != 2) {
        return 1;
    }
    if (state.comm[0] != 's' || state.comm[1] != 'w' || state.comm[4] != 0) {
        return 2;
    }
    return state.tail == 9 ? 0 : 3;
}
