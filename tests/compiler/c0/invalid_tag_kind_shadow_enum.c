enum tag_collision { ENUM_VALUE = 1 };
void wrong_visible_kind(void) {
    struct tag_collision { int member; };
    enum tag_collision *invalid_type;
    (void)invalid_type;
}
