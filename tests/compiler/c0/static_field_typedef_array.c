/* Static initializer slot ownership for an array-typed record member.
 * Repeat the GNU-preprocessed source with all five MiniC targets; GNU and
 * MiniC x86 binaries must agree, including backward designated rewrites. */
typedef int Row3[3];
typedef unsigned char FourBytes[4];

struct RowHolder {
    int before;
    Row3 cells;
    int after;
};

static const struct RowHolder designated = {
    .after = 19,
    .cells = {[2] = 13, [0] = 5, [1] = 11},
    .before = 3,
};

static const struct RowHolder backward = {
    .cells[2] = 29,
    .after = 31,
    .cells[0] = 23,
    .before = 17,
    .cells[1] = 27,
};

static const struct RowHolder overwritten = {
    .cells[0] = 41,
    .after = 43,
    .cells[2] = 47,
    .cells[0] = 53,
};

union Overlay {
    Row3 cells;
    struct { int a; int b; int c; } members;
};

static union Overlay overlay = {
    .cells = {[2] = 73, [0] = 71, [1] = 79},
};

struct ByteHolder {
    int before;
    FourBytes bytes;
    int after;
};

static const struct ByteHolder byte_holder = {
    .bytes = {[3] = 89, [1] = 83, [0] = 81},
    .after = 97,
    .before = 101,
};

static int relocation_target = 103;
struct RelocHolder {
    int *slots[2];
    Row3 values;
};
static struct RelocHolder relocated = {
    .values = {[2] = 107, [0] = 109},
    .slots[1] = &relocation_target,
};

/* GNU complete zero-length member: the following field begins at the same
 * byte offset and a positional empty initializer consumes no element slot. */
struct ZeroRecord {
    int empty[0];
    int following;
};
static struct ZeroRecord zero_braced = {{}, 5};

int main(void)
{
    if (designated.before != 3 || designated.cells[0] != 5 ||
        designated.cells[1] != 11 || designated.cells[2] != 13 ||
        designated.after != 19) return 11;
    if (backward.before != 17 || backward.cells[0] != 23 ||
        backward.cells[1] != 27 || backward.cells[2] != 29 ||
        backward.after != 31) return 12;
    if (overwritten.cells[0] != 53 || overwritten.cells[1] != 0 ||
        overwritten.cells[2] != 47 || overwritten.after != 43) return 13;
    if (overlay.cells[0] != 71 || overlay.cells[1] != 79 ||
        overlay.cells[2] != 73 ||
        overlay.members.a != 71 || overlay.members.c != 73) return 14;
    if (byte_holder.before != 101 || byte_holder.bytes[0] != 81 ||
        byte_holder.bytes[1] != 83 || byte_holder.bytes[2] != 0 ||
        byte_holder.bytes[3] != 89 || byte_holder.after != 97) return 15;
    if (relocated.slots[0] != 0 || relocated.slots[1] != &relocation_target ||
        *relocated.slots[1] != 103 || relocated.values[0] != 109 ||
        relocated.values[1] != 0 || relocated.values[2] != 107) return 16;
    if (sizeof(designated.cells) != 3U * sizeof(int) ||
        sizeof(byte_holder.bytes) != 4U * sizeof(unsigned char)) return 17;
    if (zero_braced.following != 5 || sizeof(zero_braced.empty) != 0U ||
        (char *)&zero_braced.following - (char *)&zero_braced.empty != 0)
        return 18;
    return 42;
}
