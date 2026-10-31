/* MC-063 GNU C: tagged 'struct Named;' declares no field (GCC warns),
 * but a tagless 'struct { ... };' really does declare an anonymous field.
 * GCC native exits 48 (4*10+8); MiniC x86 native currently exits 88. */
struct Named { int x; };
struct TaggedBare { struct Named; int y; };
struct TaglessAnonymous { struct { int x; }; int y; };
int main(void) {
    return (int)(sizeof(struct TaggedBare) * 10U +
                 sizeof(struct TaglessAnonymous));
}
