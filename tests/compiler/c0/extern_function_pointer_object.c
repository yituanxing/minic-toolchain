extern long (*panic_blink_like)(int state);

static int has_blink(void) {
    return panic_blink_like != 0;
}

int main(void) {
    return has_blink() ? 0 : 0;
}
