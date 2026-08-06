#include "aes.c"

static const uint8_t vector_key[16] = {
    0x2b, 0x7e, 0x15, 0x16,
    0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88,
    0x09, 0xcf, 0x4f, 0x3c
};

static const uint8_t vector_plaintext[16] = {
    0x6b, 0xc1, 0xbe, 0xe2,
    0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11,
    0x73, 0x93, 0x17, 0x2a
};

static const uint8_t vector_ciphertext[16] = {
    0x3a, 0xd7, 0x7b, 0xb4,
    0x0d, 0x7a, 0x36, 0x60,
    0xa8, 0x9e, 0xca, 0xf3,
    0x24, 0x66, 0xef, 0x97
};

static int block_mismatch_index(
    uint8_t *actual,
    const uint8_t *expected)
{
    int index;

    for (index = 0; index < 16; ++index) {
        if (actual[index] != expected[index]) {
            return index + 1;
        }
    }
    return 0;
}

int main(void)
{
    struct AES_ctx context;
    uint8_t block[16];
    int failure;
    int index;

    for (index = 0; index < 16; ++index) {
        block[index] = vector_plaintext[index];
    }

    AES_init_ctx(&context, &vector_key[0]);
    AES_ECB_encrypt(&context, &block[0]);
    failure = block_mismatch_index(&block[0], &vector_ciphertext[0]);
    if (failure != 0) {
        return failure;
    }

    AES_ECB_decrypt(&context, &block[0]);
    failure = block_mismatch_index(&block[0], &vector_plaintext[0]);
    if (failure != 0) {
        return 32 + failure;
    }

    return 0;
}
