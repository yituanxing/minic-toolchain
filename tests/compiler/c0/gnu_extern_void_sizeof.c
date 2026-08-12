extern const void opaque_symbol;

unsigned long opaque_size(void) {
    return sizeof(opaque_symbol);
}

int main(void) {
    return opaque_size() == 1UL ? 0 : 1;
}
