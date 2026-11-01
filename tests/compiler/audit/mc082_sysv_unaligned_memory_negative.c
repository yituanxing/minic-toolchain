/* GNU SysV classifies this packed unaligned u32 leaf as MEMORY, not INTEGER.
 * MC-082 extends only naturally aligned INTEGER-class transport; until the
 * <=16-byte MEMORY/sret classifier exists, MiniC must still fail closed. */
struct __attribute__((packed)) Misaligned5 {
    unsigned char prefix;
    unsigned int word;
};

struct Misaligned5 memory_return(unsigned int word) {
    struct Misaligned5 value = {0};
    value.prefix = 7U;
    value.word = word;
    return value;
}

unsigned int memory_argument(struct Misaligned5 value) {
    return value.prefix + value.word;
}

int main(void) {
    return memory_argument(memory_return(35U));
}
