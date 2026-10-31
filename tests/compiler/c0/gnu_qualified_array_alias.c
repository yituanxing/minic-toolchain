/* Linux lib/crypto/aes.c exposes two readonly aliases for internal
 * volatile/const substitution tables.  GNU alias declarations name storage,
 * and may therefore carry a different top-level element cv qualifier. */
static volatile const unsigned char aes_sbox[4] = { 0x63, 0x7c, 0x77, 0x7b };
static const volatile unsigned char aes_inv_sbox[4] = { 0x52, 0x09, 0x6a, 0xd5 };

extern const unsigned char crypto_aes_sbox[4]
    __attribute__((alias("aes_sbox")));
extern const unsigned char crypto_aes_inv_sbox[4]
    __attribute__((alias("aes_inv_sbox")));

unsigned int minic_alias_sbox_sum(void) {
    return (unsigned int)crypto_aes_sbox[0] +
           (unsigned int)crypto_aes_inv_sbox[1];
}
