typedef struct Pair { long first; long second; } Pair;
Pair invalid_return(void) {
    /* An integer statement expression is not a record return. */
    return ({ long value = 1; value; });
}
