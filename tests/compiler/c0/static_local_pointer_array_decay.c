static const unsigned char reserved_address_base[6] = {1, 2, 3, 4, 5, 6};

static int probe(void) {
    static const unsigned short *value = (const unsigned short *)reserved_address_base;
    return value == (const unsigned short *)reserved_address_base ? 0 : 1;
}

int main(void) {
    return probe();
}
