static unsigned flags;
static int calls;

static unsigned *next_flags(void) {
    calls += 1;
    return &flags;
}

int main(void) {
    flags = 255u;
    (*next_flags() &= 15u);
    return calls == 1 && flags == 15u ? 0 : 1;
}
