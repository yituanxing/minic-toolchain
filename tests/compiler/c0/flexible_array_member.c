struct __attribute__((__packed__)) packet {
    unsigned short length;
    unsigned char flags;
    char payload[];
};

char *packet_payload(struct packet *packet) {
    return packet->payload;
}

int main(void) {
    return sizeof(struct packet) == 3 ? 0 : 1;
}
