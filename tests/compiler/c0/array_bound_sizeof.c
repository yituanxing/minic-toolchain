struct Sizes {
    unsigned char pointer_bytes[(sizeof(void *))];
    int arithmetic[sizeof(long) + 2 * sizeof(short)];
};

int read_sizes(struct Sizes *sizes) {
    return sizes->pointer_bytes[7] + sizes->arithmetic[11];
}
