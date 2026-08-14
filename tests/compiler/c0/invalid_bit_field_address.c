struct Bits {
    unsigned int flag : 1;
};

unsigned int *bad(struct Bits *bits) {
    return &bits->flag;
}
