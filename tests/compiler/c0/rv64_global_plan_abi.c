/* Target-independent C oracle for RV64 GlobalEmissionPlan migration.
 * Keep each definition externally visible to ELF inspection even if a
 * compiler proves the runtime checks at compile time. */
int rv64_plan_anchor = 37;
static __attribute__((used)) unsigned char rv64_plan_byte = 255;
static __attribute__((used)) float rv64_plan_float = 1.25f;
static __attribute__((used)) double rv64_plan_double = -2.5;
static __attribute__((used)) int *rv64_plan_pointer = &rv64_plan_anchor;
static __attribute__((used)) int rv64_plan_zero_tail[5] = {11, 13};
static __attribute__((used)) int rv64_plan_tentative[4];

__attribute__((used, section(".bss.minic.rv64.plan"))) int rv64_plan_named_bss;
__attribute__((used, section(".bssish.minic.rv64.plan"))) int rv64_plan_named_data;

static int rv64_plan_add(int value) { return value + 5; }
struct Rv64PlanNested {
    int *pointer;
    int (*call)(int);
    double fraction;
};
struct Rv64PlanOuter {
    struct Rv64PlanNested nested;
    const char *name;
};
static __attribute__((used)) struct Rv64PlanOuter rv64_plan_nested = {
    {&rv64_plan_anchor, rv64_plan_add, 2.25}, "nested"
};

static __attribute__((used)) const char *rv64_plan_pointer_grid[2][2] = {
    {"first", "second"}, {"third", 0}
};

struct Rv64PlanFlexible {
    const char *name;
    int values[];
};
static __attribute__((used)) struct Rv64PlanFlexible rv64_plan_flexible = {
    "flexible", {11, 13, 17}
};

struct Rv64PlanBitfield {
    unsigned int low : 6;
    unsigned int high : 10;
    unsigned short marker;
};
static __attribute__((used)) struct Rv64PlanBitfield rv64_plan_bitfield = {
    37, 777, 0x1234
};

int main(void) {
    return rv64_plan_anchor == 37 && rv64_plan_byte == 255 &&
        rv64_plan_float == 1.25f && rv64_plan_double == -2.5 &&
        rv64_plan_pointer == &rv64_plan_anchor &&
        rv64_plan_zero_tail[0] == 11 && rv64_plan_zero_tail[1] == 13 &&
        rv64_plan_zero_tail[4] == 0 && rv64_plan_tentative[3] == 0 &&
        rv64_plan_named_bss == 0 && rv64_plan_named_data == 0 &&
        rv64_plan_nested.nested.pointer == &rv64_plan_anchor &&
        rv64_plan_nested.nested.call(5) == 10 &&
        rv64_plan_nested.nested.fraction == 2.25 &&
        rv64_plan_nested.name[0] == 'n' &&
        rv64_plan_pointer_grid[0][0][0] == 'f' &&
        rv64_plan_pointer_grid[1][0][0] == 't' &&
        rv64_plan_pointer_grid[1][1] == 0 &&
        rv64_plan_flexible.name[0] == 'f' &&
        rv64_plan_flexible.values[0] == 11 &&
        rv64_plan_flexible.values[2] == 17 &&
        rv64_plan_bitfield.low == 37 &&
        rv64_plan_bitfield.high == 777 &&
        rv64_plan_bitfield.marker == 0x1234 ? 42 : 1;
}
