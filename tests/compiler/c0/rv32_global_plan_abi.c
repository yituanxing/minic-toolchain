/* RV32 ILP32 GlobalEmissionPlan source/ELF/QEMU oracle.
 * No host headers: exactly one GNU-preprocessed .i is shared by both MiniCs
 * and, where installed, GNU's real RV32 ELF32 assembler/compiler. */
int rv32_plan_anchor[4] = {9, 11, 13, 15};
static __attribute__((used)) unsigned long long rv32_plan_wide =
    0xfedcba9876543210ULL;
static __attribute__((used)) unsigned short rv32_plan_halves[3] = {2, 3};
static __attribute__((used)) int *rv32_plan_addend = &rv32_plan_anchor[2];
static __attribute__((used)) int rv32_plan_zeros[4];

/* GNU zero-sized extensions must still own typed ELF object symbols. */
struct Rv32PlanEmpty {};
static __attribute__((used)) struct Rv32PlanEmpty rv32_plan_empty_record;
static __attribute__((used)) int rv32_plan_empty_array[0];

/* Current RV32 intentionally emits an explicitly named .bss.* as PROGBITS.
 * This differs from GNU's NOBITS policy, but R2 must preserve C39 bytes. */
__attribute__((used, section(".bss.minic.rv32.plan"))) int rv32_plan_named_bss;
__attribute__((used, section(".data.minic.rv32.plan"))) int rv32_plan_named_data = 17;

__attribute__((weak, used)) int rv32_plan_weak_zero;
__attribute__((weak, used)) int rv32_plan_weak_value = 7;
__attribute__((weak, used)) int rv32_plan_weak_array[2];

struct Rv32PlanInner {
    unsigned short number;
    int *pointer;
};
struct Rv32PlanOuter {
    struct Rv32PlanInner children[2];
    unsigned int flags;
};
static __attribute__((used, section(".init.data.minic.rv32.plan")))
struct Rv32PlanOuter rv32_plan_nested = {
    {{21, &rv32_plan_anchor[0]}, {23, &rv32_plan_anchor[2]}}, 41
};

/* A section-owned pointer grid must still use 32-bit, not LP64, fixups. */
static __attribute__((used, section(".init.data.minic.rv32.ptrs")))
int *rv32_plan_pointer_grid[3] = {
    &rv32_plan_anchor[1], 0, &rv32_plan_anchor[3]
};

int main(void) {
    return rv32_plan_anchor[0] == 9 && rv32_plan_anchor[2] == 13 &&
        rv32_plan_wide == 0xfedcba9876543210ULL &&
        rv32_plan_halves[0] == 2 && rv32_plan_halves[1] == 3 &&
        rv32_plan_halves[2] == 0 &&
        rv32_plan_addend == &rv32_plan_anchor[2] &&
        rv32_plan_zeros[3] == 0 &&
        sizeof(rv32_plan_empty_record) == 0 &&
        sizeof(rv32_plan_empty_array) == 0 &&
        rv32_plan_named_bss == 0 && rv32_plan_named_data == 17 &&
        rv32_plan_weak_zero == 0 && rv32_plan_weak_value == 7 &&
        rv32_plan_weak_array[1] == 0 &&
        rv32_plan_nested.children[0].number == 21 &&
        rv32_plan_nested.children[0].pointer == &rv32_plan_anchor[0] &&
        rv32_plan_nested.children[1].number == 23 &&
        rv32_plan_nested.children[1].pointer == &rv32_plan_anchor[2] &&
        rv32_plan_nested.flags == 41 &&
        rv32_plan_pointer_grid[0] == &rv32_plan_anchor[1] &&
        rv32_plan_pointer_grid[1] == 0 &&
        rv32_plan_pointer_grid[2] == &rv32_plan_anchor[3] ? 42 : 1;
}
