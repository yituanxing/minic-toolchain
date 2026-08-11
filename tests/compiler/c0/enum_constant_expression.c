enum Token {
    TOKEN_FIRST = (255 + 1),
    TOKEN_SECOND = TOKEN_FIRST + 2,
    TOKEN_THIRD = (2 * 3) + TOKEN_SECOND,
    WORK_OFFQ_POOL_SHIFT_LIKE = 33,
    WORK_OFFQ_LEFT_LIKE = 64 - WORK_OFFQ_POOL_SHIFT_LIKE,
    WORK_OFFQ_POOL_BITS_LIKE =
        WORK_OFFQ_LEFT_LIKE <= 31 ? WORK_OFFQ_LEFT_LIKE : 31,
    TOKEN_LOGICAL = TOKEN_SECOND > 200 && TOKEN_FIRST == 256 ? 9 : 10,
    TOKEN_SHORT_CIRCUIT = 0 ? (1 / 0) : 7
};

_Static_assert(TOKEN_FIRST == 256, "enum arithmetic");
_Static_assert(TOKEN_SECOND == 258, "prior enumerator");
_Static_assert(TOKEN_THIRD == 264, "mixed arithmetic");
_Static_assert(WORK_OFFQ_POOL_BITS_LIKE == 31, "Linux conditional enum initializer");
_Static_assert(TOKEN_LOGICAL == 9, "logical and relational enum initializer");
_Static_assert(TOKEN_SHORT_CIRCUIT == 7, "conditional evaluation selects one branch");

int main(void) {
    return TOKEN_FIRST == 256 && TOKEN_SECOND == 258 && TOKEN_THIRD == 264 &&
                   WORK_OFFQ_POOL_BITS_LIKE == 31 && TOKEN_LOGICAL == 9 &&
                   TOKEN_SHORT_CIRCUIT == 7
               ? 0
               : 1;
}
