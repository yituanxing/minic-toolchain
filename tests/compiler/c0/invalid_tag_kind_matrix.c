/* All cross-kind collisions use the same tag and declaration grammar.
 * SHADOW tests lookup after a legal inner declaration; FORWARD tests a
 * cross-kind forward reference rather than a second definition. */
#define TAG_ENUM enum tag_collision { ENUM_VALUE = 1 }
#define TAG_STRUCT struct tag_collision { int member; }
#define TAG_UNION union tag_collision { int member; }
FIRST;
#if defined(SHADOW)
void wrong_visible_kind(void) {
    SECOND;
    WRONG tag_collision *invalid_type;
    (void)invalid_type;
}
#elif defined(FORWARD)
void wrong_forward_kind(void) {
    enum tag_collision;
}
#else
SECOND;
#endif
