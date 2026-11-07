/* Zero-byte record transfer has no memory effect, but both pointer-valued
 * source expressions must still execute before the (empty) assignment. */
struct TinyEmpty { ; };
struct TinyHolder { struct TinyEmpty value; };
static int visits;
static unsigned char guard[2] = {0xA7U, 0x6CU};

static struct TinyEmpty *observe(struct TinyEmpty *p) {
    ++visits;
    return p;
}

static void assign_via_calls(struct TinyEmpty *dst, struct TinyEmpty *src) {
    *observe(dst) = *observe(src);
}

static void assign_statement_value(struct TinyHolder *dst) {
    dst->value = ({ struct TinyEmpty item = {}; item; });
}

int main(void) {
    struct TinyHolder holder;
    struct TinyEmpty *p = (struct TinyEmpty *)(void *)&guard[0];
    assign_via_calls(p, p);
    assign_statement_value(&holder);
    if (visits != 2) return 1;
    if (guard[0] != 0xA7U || guard[1] != 0x6CU) return 2;
    return 0;
}
