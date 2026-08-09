enum Token {
    TOKEN_FIRST = (255 + 1),
    TOKEN_SECOND = TOKEN_FIRST + 2,
    TOKEN_THIRD = (2 * 3) + TOKEN_SECOND
};

int main(void) {
    return TOKEN_FIRST == 256 && TOKEN_SECOND == 258 && TOKEN_THIRD == 264 ? 0 : 1;
}
