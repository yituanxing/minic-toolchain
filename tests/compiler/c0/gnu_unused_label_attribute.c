static int probe(int value) {
    if (value) {
        goto done;
    }
    value = 7;
done:
    __attribute__((__unused__));
    return value;
}
int main(void) {
    return probe(0) == 7 ? 0 : 1;
}
