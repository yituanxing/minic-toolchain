extern __attribute__((__externally_visible__)) const void __nosave_begin, __nosave_end;

int main(void) {
    return &__nosave_begin != &__nosave_end;
}
