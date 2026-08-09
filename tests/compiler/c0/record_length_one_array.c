struct Holder {
    int values[1];
};

int read_first(struct Holder *holder) {
    return holder->values[0];
}

int main(void) {
    struct Holder holder;
    holder.values[0] = 7;
    return sizeof(holder) == 4 && read_first(&holder) == 7 ? 0 : 1;
}
