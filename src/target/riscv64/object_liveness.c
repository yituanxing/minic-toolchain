#include "target/riscv64/object_liveness.h"
#include "target/data_layout.h"
bool minic_riscv64_object_reuse_plan_build(
    const MinicC0Program *p, const MinicCoreFunction *f,
    bool crossblock, MinicRiscv64ObjectReusePlan *plan) {
    return minic_core_object_reuse_plan_build(
        p, f, minic_default_data_layout(), 8U, crossblock, plan);
}
bool minic_riscv64_object_reuse_plan_get(
    const MinicRiscv64ObjectReusePlan *plan, MinicCoreObjectId id,
    bool *reusable, size_t *slot, size_t *peak) {
    return minic_core_object_reuse_plan_get(plan, id, reusable, slot, peak);
}
void minic_riscv64_object_reuse_plan_destroy(MinicRiscv64ObjectReusePlan *plan) {
    minic_core_object_reuse_plan_destroy(plan);
}
