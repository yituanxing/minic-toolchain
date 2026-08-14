struct Holder {
    int values[1];
    int scalar;
};

int read_first(struct Holder *holder) {
    return holder->values[0] + holder->scalar;
}
