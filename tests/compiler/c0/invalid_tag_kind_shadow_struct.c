struct tag_collision { int member; };
void wrong_visible_kind(void) {
    enum tag_collision { ENUM_VALUE = 1 };
    struct tag_collision *invalid_type;
    (void)invalid_type;
}
