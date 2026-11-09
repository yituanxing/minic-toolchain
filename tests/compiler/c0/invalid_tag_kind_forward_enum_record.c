struct tag_collision { int member; };
void wrong_forward_kind(void) {
    enum tag_collision;
}
