struct Packet {
    int tag;
    unsigned char bytes[4];
    int value;
};

int copy_packet(void) {
    struct Packet source;
    struct Packet target;

    target = source;
    return 0;
}

int main(void) {
    return copy_packet();
}
