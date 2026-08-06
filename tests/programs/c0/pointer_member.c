typedef int byte;

struct Packet {
    int prefix;
    byte data[4];
    int value;
};

static int sum_four(int *values)
{
    return values[0] + values[1] + values[2] + values[3];
}

static int exercise(struct Packet *packet)
{
    packet->prefix = 9;
    packet->data[0] = 2;
    packet->data[1] = 4;
    packet->value = 7;
    packet->data[2] = packet->value;
    packet->data[3] = 3;
    packet->value = packet->value + 5;
    return packet->prefix + sum_four(packet->data) + packet->value;
}

int main(void)
{
    struct Packet packet;

    return exercise(&packet);
}
