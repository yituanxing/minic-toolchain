typedef int flag_t;
typedef void (*hook_t)(int);

struct State {
    volatile flag_t trap;
    const volatile int status;
    int * volatile cursor;
    volatile hook_t hook;
};

int read_state(struct State *state) {
    volatile int copy;
    copy = state->trap;
    return copy + state->status + *state->cursor;
}
