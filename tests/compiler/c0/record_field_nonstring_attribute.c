struct ext4_like_super_block {
    char s_last_mounted[64] __attribute__((__nonstring__));
};

int main(void) {
    struct ext4_like_super_block value;
    value.s_last_mounted[0] = 'x';
    return value.s_last_mounted[0] == 'x' ? 0 : 1;
}
