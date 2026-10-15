struct signed_bits {
    unsigned int tag : 2;
    int depth : 30;
};

int post_inc(struct signed_bits *p) { return p->depth++; }
int pre_inc(struct signed_bits *p) { return ++p->depth; }
int post_dec(struct signed_bits *p) { return p->depth--; }
int pre_dec(struct signed_bits *p) { return --p->depth; }

struct unsigned_bits { unsigned int value : 3; };
unsigned int unsigned_pre(struct unsigned_bits *p) { return ++p->value; }
