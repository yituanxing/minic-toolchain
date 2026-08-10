enum lockdep_like {
    LOCKDEP_LIKE_OK,
    LOCKDEP_LIKE_BAD,
};

extern enum system_states_like {
    SYSTEM_BOOTING_LIKE,
    SYSTEM_RUNNING_LIKE,
} system_state_like;

typedef enum system_states_like system_state_alias;

extern void add_taint_like(unsigned flag, enum lockdep_like state);

enum lockdep_like report_bug_like(unsigned long address, enum lockdep_like state);

static enum lockdep_like normalize_state(enum lockdep_like state) {
    return state;
}

int main(void) {
    system_state_alias state = SYSTEM_BOOTING_LIKE;

    return normalize_state(LOCKDEP_LIKE_OK) + state;
}
