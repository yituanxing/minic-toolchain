union Holder {
    int value;
    long wide;
};

int read_holder(void *raw) {
    union Holder *holder = (union Holder *)raw;
    volatile int *value = (volatile int *)&holder->value;
    return *value;
}

int main(void) {
    union Holder holder;
    holder.value = 9;
    return read_holder(&holder) == 9 ? 0 : 1;
}
