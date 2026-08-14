typedef unsigned int u32;

static const u32 runnable_avg_yN_inv[] __attribute__((__unused__)) = {
    0xffffffff,
    2,
    3,
};

int main(void) {
    return sizeof(runnable_avg_yN_inv) == 3U * sizeof(u32) ? 0 : 1;
}
