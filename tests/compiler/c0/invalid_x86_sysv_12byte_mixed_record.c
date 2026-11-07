/* A mixed SSE/INTEGER SysV aggregate is NOT two INTEGER-class registers.
 * In particular, 12-byte all-INTEGER admission may not accept this layout. */
typedef struct MixedWords {
    float first;
    unsigned int second;
    unsigned int third;
} MixedWords;

MixedWords invalid_mixed_return(MixedWords input) {
    return input;
}
