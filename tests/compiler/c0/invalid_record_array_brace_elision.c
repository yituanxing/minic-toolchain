struct packet {
    int values[2];
    int tail;
};

int invalid_record_array_brace_elision(void) {
    struct packet packet = {1, 2, 3};
    return packet.tail;
}
