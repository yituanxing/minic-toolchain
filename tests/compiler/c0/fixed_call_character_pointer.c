static long take_unsigned(const unsigned char *p) {
    return p[0];
}
static long take_signed(const signed char *p) {
    return p[0];
}
int main(void) {
    /* The bridge belongs to fixed-call conversion; the local remains plain char. */
    char bytes[1] = {0};
    return (int)(take_unsigned(bytes) + take_signed(bytes));
}
