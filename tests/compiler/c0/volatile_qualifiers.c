typedef int flag_t;

struct State {
    volatile flag_t trap;
    const volatile int status;
};

int read_state(struct State *state) {
    volatile int copy;
    copy = state->trap;
    return copy + state->status;
}
