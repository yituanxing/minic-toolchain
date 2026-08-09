struct short_layout {
    unsigned char first;
    unsigned short second;
    unsigned int third;
};

static struct short_layout layout;
static unsigned short initialized = 65535;

unsigned short read_second(void) {
    return layout.second;
}

void write_second(unsigned short value) {
    layout.second = value;
}

short narrow_signed(int value) {
    return value;
}

int main(void) {
    write_second(initialized);
    return read_second() == 65535 && narrow_signed(65535) == -1 ? 0 : 1;
}
