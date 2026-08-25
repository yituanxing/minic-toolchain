int probe_digit(int value) {
    return __builtin_isdigit(value);
}

int main(void) {
    return probe_digit('7') && !probe_digit('x') && !probe_digit(-1) ? 0 : 1;
}
