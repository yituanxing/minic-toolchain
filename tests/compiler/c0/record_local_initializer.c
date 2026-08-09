struct Packet {
    int tag;
    unsigned char bytes[4];
};

static int copy_packet(struct Packet *source) {
    struct Packet saved = *source;
    return saved.tag;
}

int main(void) {
    return 0;
}
