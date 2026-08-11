int main(void) {
    int value = 0;
    const void *left = &value;
    void *right = &value;

    if (left < right) {
        return 1;
    }
    if (left > right) {
        return 2;
    }
    if (left <= right && left >= right) {
        return 0;
    }
    return 3;
}
