/* The array type behind a pointer remains compatible when one bound is
 * incomplete. This occurs in Linux i915 DMI pointer initializers. */
struct Entry { unsigned id; };

static const struct Entry entries[2] = {{ 11U }, { 22U }};
struct Holder { const struct Entry (*list)[]; };
static const struct Holder holder = { .list = &entries };
static const struct Holder compound_holder = {
    .list = &(const struct Entry[]){ { 33U }, { 44U } }
};

int main(void) {
    return (*holder.list)[0].id == 11U &&
                   (*holder.list)[1].id == 22U &&
                   (*compound_holder.list)[0].id == 33U &&
                   (*compound_holder.list)[1].id == 44U
               ? 0 : 1;
}
