struct Packet {
    int tag;
    unsigned char bytes[4];
};

struct Guard {
    void *lock;
    int flags;
};

static int copy_packet(struct Packet *source) {
    struct Packet saved = *source;
    return saved.tag;
}

static int initialize_guard(void) {
    struct Guard guard = { .lock = (void *)1, .flags = 7 },
                 *guard_ptr __attribute__((__unused__)) = &guard;
    return (unsigned long)guard.lock == 1 && guard.flags == 7 && guard_ptr == &guard;
}

int main(void) {
    return initialize_guard() ? 0 : 1;
}
