enum slot_index { SLOT_ONE = 1, SLOT_THREE = 3 };

struct pair {
    int left;
    int right;
};

struct holder {
    struct pair limits[4];
};

typedef struct pair pair_t;

static const struct pair sparse_records[] = {
    [SLOT_ONE] = {.left = 11, .right = 12},
    [SLOT_THREE] = (struct pair){.left = 31, .right = 32},
};

static const struct holder nested_records = {
    .limits =
        {
            [1] = {.left = 21, .right = 22},
            [3] = {.left = 41, .right = 42},
        },
};

static const pair_t fixed_records[4] = {
    [2] = ((pair_t){.left = 51, .right = 52}),
};

static int relocation_target;

union relocation_op {
    int (*show)(void);
    const char *lsm;
};

struct relocation_row {
    int *target;
    union relocation_op op;
};

static const struct relocation_row relocation_records[] = {
    {.target = &relocation_target, .op = {.lsm = "apparmor"}},
};

/* Linux lib/atomic64.c uses a static array of padded union lock slots and a
   GNU range designator. A complete union is just as valid an array element
   type as a complete struct, including in a constant initializer. */
union padded_lock_slot {
    struct {
        int state;
        int owner;
    } lock;
    char padding[64];
};

static union padded_lock_slot atomic_lock_slots[16] = {
    [0 ... 15] = {.lock = {.state = 1, .owner = -1}},
};

/* Linux init/init_task.c: an already zero-filled nested object is written
 * through a compound literal with a backward GNU range designator. The
 * single evaluated scalar constant must populate every designated slot. */
typedef struct { unsigned long bits[4]; } cpu_mask_t;
struct initial_task_mask { cpu_mask_t cpus_mask; };
static struct initial_task_mask task_mask = {
    .cpus_mask = (cpu_mask_t){{[0 ... 2] = ~0UL, [3] = 7UL}},
};

int main(void) {
    return sparse_records[1].left == 11 && sparse_records[3].right == 32 &&
                   nested_records.limits[1].right == 22 && nested_records.limits[3].left == 41 &&
                   fixed_records[2].right == 52 && atomic_lock_slots[0].lock.state == 1 &&
                   atomic_lock_slots[15].lock.owner == -1 &&
                   task_mask.cpus_mask.bits[0] == ~0UL &&
                   task_mask.cpus_mask.bits[1] == ~0UL &&
                   task_mask.cpus_mask.bits[2] == ~0UL &&
                   task_mask.cpus_mask.bits[3] == 7UL
               ? 0
               : 1;
}
