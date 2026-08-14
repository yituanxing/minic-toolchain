struct counter {
    int value;
};

union key_payload {
    unsigned long type;
    void *entries;
};

struct static_key {
    struct counter enabled;
    union key_payload payload;
};

struct static_key_false {
    struct static_key key;
};

struct static_key_false sched_numa_balancing = (struct static_key_false){
    .key = {.enabled = {0}, {.type = 0UL}},
};

int main(void) {
    return sched_numa_balancing.key.enabled.value;
}
