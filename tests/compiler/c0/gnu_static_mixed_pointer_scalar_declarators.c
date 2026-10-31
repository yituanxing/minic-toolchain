/* A declarator's `*` belongs to that name, not the shared declaration specifier.
 * In particular, after an initialized pointer, the next name may be either
 * another pointer or an ordinary scalar object. */
static int left = 11;
static int right = 29;
static int *first = &left, *second = &right, scalar = 37;
static const char *first_text = "left", *second_text = "right";
static int * const protected = &left, *ordinary = &right;
typedef int *int_pointer;
static int_pointer from_typedef = &left, also_from_typedef = &right;

int main(void) {
    return *first == 11 && *second == 29 && scalar == 37 &&
                   first_text[0] == 'l' && second_text[0] == 'r' &&
                   *protected == 11 && *ordinary == 29 &&
                   *from_typedef == 11 && *also_from_typedef == 29 ? 0 : 1;
}
