/* Differential object-layout oracle for the shared GlobalEmissionPlan.
 * One TU exercises ELF metadata, scalar IEEE754 data, relocations and
 * recursive C aggregate initialization without target instruction tricks. */
int global_anchor = 37;
static __attribute__((used)) float global_float = 1.25f;
static __attribute__((used)) double global_double = -2.5;
static __attribute__((used)) int *global_pointer = &global_anchor;

__attribute__((used, section(".bss.minic.global"))) int named_bss_zero;
__attribute__((used, section(".bssish.minic.global"))) int named_bssish_zero;

static int global_function(int value) { return value + 5; }
struct NestedReloc {
    int *pointer;
    int (*call)(int);
    double fraction;
};
struct OuterReloc {
    struct NestedReloc nested;
    const char *name;
};
static __attribute__((used)) struct OuterReloc global_nested = {
    { &global_anchor, global_function, 2.25 }, "nested"
};

static __attribute__((used)) const char *global_pointer_grid[2][2] = {
    { "first", "second" }, { "third", 0 }
};

struct FlexibleGlobal {
    const char *name;
    int values[];
};
static __attribute__((used)) struct FlexibleGlobal global_flexible = {
    "flexible", { 11, 13, 17 }
};

int main(void) {
    return global_anchor == 37 && global_float == 1.25f &&
           global_double == -2.5 && global_pointer == &global_anchor &&
           named_bss_zero == 0 && named_bssish_zero == 0 &&
           global_nested.nested.pointer == &global_anchor &&
           global_nested.nested.call(5) == 10 &&
           global_nested.nested.fraction == 2.25 &&
           global_nested.name[0] == 'n' &&
           global_pointer_grid[0][0][0] == 'f' &&
           global_pointer_grid[1][0][0] == 't' &&
           global_pointer_grid[1][1] == 0 &&
           global_flexible.name[0] == 'f' &&
           global_flexible.values[0] == 11 &&
           global_flexible.values[2] == 17 ? 0 : 1;
}
