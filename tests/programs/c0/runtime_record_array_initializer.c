typedef unsigned char u8;

typedef struct {
    u8 b[16];
} guid_t;

typedef struct {
    u8 one[1];
    u8 bytes[4];
    int tail;
} packet_t;

static int guid_score(const guid_t *guid) {
    return guid->b[0] + guid->b[1] + guid->b[15];
}

static int packet_score(const packet_t *packet) {
    return packet->one[0] + packet->bytes[0] + packet->bytes[1] + packet->bytes[2] +
           packet->bytes[3] + packet->tail;
}

int main(void) {
    int score;
    packet_t positional;
    packet_t designated;

    score = guid_score(&(guid_t){
        {0x61 & 0xff, (0x61 >> 1) & 0xff, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}});
    positional = (packet_t){{7}, {2, 3}, 5};
    designated = (packet_t){.one = {9}, .bytes = {4, 5, 6}, .tail = 8};
    score += packet_score(&positional);
    score += packet_score(&designated);
    return score;
}
