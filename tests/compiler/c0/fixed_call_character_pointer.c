static long take_unsigned(const unsigned char *p) {
    return p[0];
}
static long take_signed(const signed char *p) {
    return p[0];
}
int main(void) {
    char bytes[1] = {0};
    return (int)(take_unsigned(bytes) + take_signed(bytes));
}
