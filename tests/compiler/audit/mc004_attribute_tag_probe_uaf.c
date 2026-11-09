/* MC-004: GNU aligned attribute parses a semantic sizeof that creates a tag.
 * With sixteen prior tag bindings, the historical shallow Parser probe moved
 * its tags arena and left the original Parser pointing at freed storage. */
struct Pad01 { int value; };
struct Pad02 { int value; };
struct Pad03 { int value; };
struct Pad04 { int value; };
struct Pad05 { int value; };
struct Pad06 { int value; };
struct Pad07 { int value; };
struct Pad08 { int value; };
struct Pad09 { int value; };
struct Pad10 { int value; };
struct Pad11 { int value; };
struct Pad12 { int value; };
struct Pad13 { int value; };
struct Pad14 { int value; };
struct Pad15 { int value; };
#ifndef MC004_SKIP_PAD16
struct Pad16 { int value; };
#endif
#ifdef MC004_ADD_PAD17
struct Pad17 { int value; };
#endif

int aligned_record __attribute__((aligned(sizeof(struct AttributeProbeTag {
    int field;
})))) = 1;

/* GNU makes a tagged definition in an attribute's integer expression visible
 * to later declarations in the same scope. Preserve that semantic ownership. */
struct AttributeProbeTag visible_record = {7};

int main(void) {
    return visible_record.field == 7 && sizeof(struct Pad01) == sizeof(int) &&
                   sizeof(struct AttributeProbeTag) == sizeof(int) && aligned_record == 1
               ? 0
               : 31;
}
