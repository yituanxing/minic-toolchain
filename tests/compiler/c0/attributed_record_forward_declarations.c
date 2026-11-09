/* MC-067: GNU record attributes before or after the forward tag must not
 * turn a declaration into a definition (at file or block scope). */
struct __attribute__((aligned(16))) Before;
struct After __attribute__((aligned(16)));
union __attribute__((may_alias)) UnionBefore;
union UnionAfter __attribute__((may_alias));

struct Before { int x; };
struct After { int x; };
union UnionBefore { int x; };
union UnionAfter { int x; };

int main(void) {
    struct __attribute__((aligned(16))) LocalBefore;
    struct LocalAfter __attribute__((aligned(16)));
    union __attribute__((may_alias)) LocalUnionBefore;
    union LocalUnionAfter __attribute__((may_alias));
    struct LocalBefore { int x; };
    struct LocalAfter { int x; };
    union LocalUnionBefore { int x; };
    union LocalUnionAfter { int x; };

    return sizeof(struct Before) != sizeof(int) ||
           sizeof(struct After) != sizeof(int) ||
           sizeof(union UnionBefore) != sizeof(int) ||
           sizeof(union UnionAfter) != sizeof(int) ||
           sizeof(struct LocalBefore) != sizeof(int) ||
           sizeof(struct LocalAfter) != sizeof(int) ||
           sizeof(union LocalUnionBefore) != sizeof(int) ||
           sizeof(union LocalUnionAfter) != sizeof(int);
}
