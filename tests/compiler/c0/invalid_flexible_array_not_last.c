struct invalid_packet {
    int length;
    char payload[];
    int trailing;
};

int main(void) {
    return 0;
}
