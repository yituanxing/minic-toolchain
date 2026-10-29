/* Switching active union members in one initializer is not the
 * same as initializing two promoted fields of the active record.
 * GNU diagnoses the override under -Werror=override-init. */
union active {
    unsigned long long val;
    struct { unsigned long long first:4, second:4; };
};
static union active bad = {.first = 3, .val = 8};
int main(void) { return (int)bad.val; }
