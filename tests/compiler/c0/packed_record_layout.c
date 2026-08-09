struct __attribute__((__packed__)) packed_sample {
    unsigned char first;
    unsigned short second;
    unsigned char third;
};

static struct packed_sample sample;

unsigned short read_second(void) {
    return sample.second;
}

unsigned char read_third(void) {
    return sample.third;
}

int main(void) {
    return sizeof(struct packed_sample) == 4 ? 0 : 1;
}
