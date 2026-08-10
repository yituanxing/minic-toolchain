enum lockdep_like {
    LOCKDEP_LIKE_OK,
    LOCKDEP_LIKE_BAD,
};

extern void add_taint_like(unsigned flag, enum lockdep_like state);

static enum lockdep_like normalize_state(enum lockdep_like state) {
    return state;
}

int main(void) {
    return normalize_state(LOCKDEP_LIKE_OK);
}
