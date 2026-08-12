struct __attribute__((__packed__)) packed_sample {
    unsigned char first;
    unsigned short second;
    unsigned char third;
};

struct suffix_packed_sample {
    unsigned char first;
    unsigned short second;
    unsigned char third;
} __attribute__((__packed__));

struct forward_packed_sample;
struct __attribute__((__packed__)) forward_packed_sample {
    unsigned char first;
    unsigned short second;
};

static struct packed_sample sample;
static struct suffix_packed_sample suffix_sample;
static struct forward_packed_sample forward_sample;

unsigned short read_second(void) {
    return sample.second;
}

unsigned char read_third(void) {
    return sample.third;
}

int main(void) {
    return sizeof(struct packed_sample) == 4 && sizeof(struct suffix_packed_sample) == 4 &&
                   sizeof(struct forward_packed_sample) == 3
               ? 0
               : 1;
}
