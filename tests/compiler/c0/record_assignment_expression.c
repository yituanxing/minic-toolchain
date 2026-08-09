union Payload {
    long wide;
    int words[2];
};

int main(void) {
    union Payload left;
    union Payload right;

    right.wide = 123;
    (left = right, (void)0);
    return left.wide == 123 ? 0 : 1;
}
