typedef unsigned int u32;

typedef int (*signed_pointer_call_t)(int *old, int next);

static int update_signed(int *old, int next) {
    int previous = *old;
    *old = next;
    return previous;
}

static int direct_pointer_sign_call(void) {
    u32 old = 7U;
    int previous = update_signed(&old, old + 3U);

    return previous == 7 && old == 10U ? 0 : 1;
}

static int indirect_pointer_sign_call(signed_pointer_call_t call) {
    u32 old = 11U;
    int previous = call(&old, old + 4U);

    return previous == 11 && old == 15U ? 0 : 2;
}

int main(void) {
    return direct_pointer_sign_call() + indirect_pointer_sign_call(update_signed);
}
