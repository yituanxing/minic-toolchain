struct AES_ctx {
    int RoundKey[176];
};

void AES_init_ctx(struct AES_ctx *ctx, const int *key);

int main(void)
{
    return 0;
}
