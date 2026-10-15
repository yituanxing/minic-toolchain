int nested_label_must_not_be_dropped(void) {
    goto inside;
    return 1;
    if (1) {
inside:
        return 2;
    }
}
