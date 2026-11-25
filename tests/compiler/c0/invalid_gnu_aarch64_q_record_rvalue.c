/* An aggregate passed to a GNU memory constraint must remain an lvalue. */
struct minic_q_word { unsigned int bits; };
extern struct minic_q_word minic_q_make_record(void);

unsigned int minic_q_invalid_record_rvalue(void) {
    unsigned int result;
    __asm__ __volatile__("ldar %w0, %1"
                         : "=r" (result)
                         : "Q" (minic_q_make_record())
                         : "memory");
    return result;
}
