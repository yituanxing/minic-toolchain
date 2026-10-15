struct signed_bits {
    unsigned int tag : 2;
    int depth : 30;
};

void clear_depth(struct signed_bits *p) {
    p->depth = 0;
}

void set_negative_depth(struct signed_bits *p) {
    p->depth = -1;
}

int assigned_signed_depth(struct signed_bits *p) {
    return (p->depth = -3);
}
