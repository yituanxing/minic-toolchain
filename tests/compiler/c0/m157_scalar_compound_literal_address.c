int scalar_zero(void) {
    return (int){0};
}

int scalar_one(void) {
    return (int){1};
}

int scalar_build_bug_shape(void) {
    return !(!((int){0} != 0));
}

int scalar_reinitialize(int x) {
    return (int){x};
}
