/* Native ABI oracle for the constant-false record call-argument fixture.
 * Keep the sink in a separate GNU-compiled translation unit: MiniC must
 * materialize and transfer the actual struct, not merely return zero. */
typedef struct {
    unsigned long bits;
} record_word_t;

void consume_word(record_word_t value) {
    if (value.bits != 11UL) {
        __builtin_trap();
    }
}
