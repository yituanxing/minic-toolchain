/* The target-neutral Core POINTER_OFFSET accepts 128-bit signed/unsigned
 * subscripts. On LP64 the low 64 bits determine the scaled displacement;
 * signed -3 has an all-ones high limb, so this exercises the wide SSA slot.
 * GNU complete empty records additionally have a zero-byte pointer stride. */
typedef signed __int128 signed128;
typedef unsigned __int128 unsigned128;
struct ZeroWidth { };
static struct ZeroWidth zero_objects[3];
static volatile long signed_source = -3;
static volatile unsigned long unsigned_source = 2;
static int values[16];

int main(void) {
    signed128 back = (signed128)signed_source;
    unsigned128 forward = (unsigned128)unsigned_source;
    int *origin;
    int *left;
    int *right;
    struct ZeroWidth *zero;
    values[4] = 11;
    values[9] = 19;
    origin = &values[7];
    left = origin + back;
    right = origin + forward;
    if (left != &values[4] || right != &values[9]) return 1;
    if (*left != 11 || *right != 19) return 2;
    zero = zero_objects;
    if (zero + back != zero || zero + forward != zero) return 3;
    return 42;
}
