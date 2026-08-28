union Payload {
    long wide;
    int words[2];
};

struct Stamp {
    long sec;
    long nsec;
};

struct Holder {
    struct Stamp first;
    struct Stamp second;
};

extern struct Stamp make_stamp(long value);

void assign_both(struct Holder *holder, long value) {
    holder->first = holder->second = make_stamp(value);
}

int main(void) {
    union Payload left;
    union Payload right;

    right.wide = 123;
    (left = right, (void)0);
    return left.wide == 123 ? 0 : 1;
}
