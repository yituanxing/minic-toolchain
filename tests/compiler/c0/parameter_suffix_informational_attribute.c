static int add_one(int value __attribute__((__unused__))) {
    return value + 1;
}
int main(void) {
    return add_one(6) == 7 ? 0 : 1;
}
