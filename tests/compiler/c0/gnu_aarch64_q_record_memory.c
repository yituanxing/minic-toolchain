/* AArch64 GCC Q is an addressable base-register-only memory operand.
 * The record is carried by its original address, not passed as an integer. */
struct minic_q_word { unsigned int bits; };

unsigned int minic_q_read_record(const struct minic_q_word *word) {
    unsigned int result;
    __asm__ __volatile__("ldar %w0, %1"
                         : "=r" (result)
                         : "Q" (*word)
                         : "memory");
    return result;
}
