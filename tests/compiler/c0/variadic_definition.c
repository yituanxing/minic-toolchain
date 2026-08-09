static int keep_fixed(int value, ...) {
    return value;
}

int main(void) {
    return keep_fixed(0, 1, 2);
}
