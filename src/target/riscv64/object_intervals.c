#include "target/riscv64/object_intervals.h"
#include "target/data_layout.h"
void minic_riscv64_object_intervals_index(
    const MinicCoreFunction *function, MinicRiscv64ObjectIntervalAnalysis *analysis) {
    minic_core_object_intervals_index(function, analysis);
}
bool minic_riscv64_object_interval(
    const MinicC0Program *p, const MinicCoreFunction *f, MinicCoreObjectId id,
    const MinicRiscv64ObjectIntervalAnalysis *analysis, CoreScalarObjectInterval *interval) {
    return minic_core_object_interval(p, f, id, minic_default_data_layout(),
                                      8U, analysis, interval);
}
void minic_riscv64_object_intervals_destroy(MinicRiscv64ObjectIntervalAnalysis *analysis) {
    minic_core_object_intervals_destroy(analysis);
}
