/* A block-scope tag is not visible after leaving the block. */
int escaped_enum_tag(void) {
    {
        enum vanishing_tag { VANISHING_VALUE = 1 };
    }
    return (int)sizeof(enum vanishing_tag);
}
