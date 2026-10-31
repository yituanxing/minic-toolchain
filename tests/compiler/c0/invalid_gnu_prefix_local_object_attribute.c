int main(void) {
    __attribute__((__cleanup__(missing_cleanup_function))) int value = 1;
    return value;
}
