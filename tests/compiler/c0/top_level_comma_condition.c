static int cursor;

int comma_condition_loop(void) {
    int iterations = 0;

    cursor = 0;
    while (((void)(cursor += 1)), (cursor < 3)) {
        iterations += 1;
    }
    return iterations == 2 && cursor == 3;
}
