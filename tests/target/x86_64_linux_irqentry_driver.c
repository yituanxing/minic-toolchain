typedef struct linux_irqentry_state {
    union { _Bool exit_rcu; _Bool lockdep; };
} linux_irqentry_state_t;

extern linux_irqentry_state_t linux_irqstate_make(_Bool);
extern int linux_irqstate_consume(linux_irqentry_state_t);
extern int linux_irqstate_roundtrip(int);
extern int linux_irqstate_from_gnu(int);

linux_irqentry_state_t gnu_irqstate_make(_Bool value) {
    linux_irqentry_state_t result;
    result.lockdep = value;
    return result;
}

int main(void) {
    linux_irqentry_state_t state = linux_irqstate_make(1);
    if (!state.exit_rcu || !state.lockdep) return 1;
    state = linux_irqstate_make(0);
    if (state.exit_rcu || state.lockdep) return 2;
    state = gnu_irqstate_make(1);
    if (linux_irqstate_consume(state) != 1) return 3;
    state = gnu_irqstate_make(0);
    if (linux_irqstate_consume(state) != 0) return 4;
    if (linux_irqstate_roundtrip(1) != 1 ||
        linux_irqstate_roundtrip(0) != 0) return 5;
    if (linux_irqstate_from_gnu(1) != 1 ||
        linux_irqstate_from_gnu(0) != 0) return 6;
    return 0;
}
