/* Linux irqentry_state_t: one SysV INTEGER eightbyte with an anonymous
 * Boolean union. GCC returns/passes the entire record in AL/DIL. */
typedef struct linux_irqentry_state {
    union {
        _Bool exit_rcu;
        _Bool lockdep;
    };
} linux_irqentry_state_t;

linux_irqentry_state_t linux_irqstate_make(_Bool value) {
    linux_irqentry_state_t result;
    result.exit_rcu = value;
    return result;
}

int linux_irqstate_consume(linux_irqentry_state_t state) {
    return state.lockdep ? 1 : 0;
}

int linux_irqstate_roundtrip(int value) {
    return linux_irqstate_consume(linux_irqstate_make(value != 0));
}

extern linux_irqentry_state_t gnu_irqstate_make(_Bool);
int linux_irqstate_from_gnu(int value) {
    linux_irqentry_state_t state = gnu_irqstate_make(value != 0);
    return state.exit_rcu ? 1 : 0;
}
