typedef unsigned char byte;

static const byte table[4] = {0, 127, 128, 255};

struct Packet {
    byte prefix;
    int payload;
    byte suffix;
};

byte narrow(int value)
{
    return (byte)value;
}

byte store_at(byte *base, int index, int value)
{
    base[index] = value;
    return base[index];
}

int fill_packet(struct Packet *packet)
{
    packet->prefix = 257;
    packet->suffix = 258;
    return packet->prefix + packet->suffix;
}

int main(void)
{
    byte values[3];
    byte *cursor;
    byte left;
    byte right;
    struct Packet packet;
    int score;

    values[0] = 300;
    values[1] = 0;
    values[2] = 0;
    cursor = &values[0];
    left = 200;
    right = 100;
    score = 0;

    if (values[0] == 44) {
        score = score + 1;
    }
    if (store_at(cursor, 1, 258) == 2) {
        score = score + 2;
    }
    cursor = cursor + 1;
    if (*cursor == 2) {
        score = score + 4;
    }
    if (narrow(511) == 255) {
        score = score + 8;
    }
    if (table[2] + table[3] == 383) {
        score = score + 16;
    }
    if (fill_packet(&packet) == 3) {
        score = score + 32;
    }
    if (left + right == 300) {
        score = score + 64;
    }
    return score;
}
