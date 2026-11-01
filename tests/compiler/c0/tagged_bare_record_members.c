/* MC-063: only a directly written, tagless struct/union contributes an
 * anonymous member. Tagged and typedef-only declarations do not add fields. */
struct Full { int x; };
union NamedUnion { int x; };
typedef struct { int z; } Alias;

struct TaggedBare { struct Full; int y; };
struct TaggedUnionBare { union NamedUnion; int y; };
struct AliasBare { Alias; int y; };
struct Incomplete;
struct IncompleteBare { struct Incomplete; int y; };

struct Anonymous { struct { int x; }; int y; };
struct ExplicitMember { struct Full inner; int y; };

int main(void) {
    struct Anonymous value;
    value.x = 17;
    value.y = 5;
    return sizeof(struct TaggedBare) != 4U ||
           sizeof(struct TaggedUnionBare) != 4U ||
           sizeof(struct AliasBare) != 4U ||
           sizeof(struct IncompleteBare) != 4U ||
           sizeof(struct Anonymous) != 8U ||
           sizeof(struct ExplicitMember) != 8U ||
           value.x + value.y != 22;
}
