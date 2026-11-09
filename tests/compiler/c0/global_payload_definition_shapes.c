/* P65: keep definition/linker policy separate from source initializer syntax.
 * In Core ModuleIR the one ZERO fragment may describe both tentative and
 * explicitly initialized weak definitions; no C slot count is published. */
__attribute__((used, weak)) int weak_tentative;
__attribute__((used, weak)) int weak_explicit_zero = 0;
__attribute__((used, weak)) int weak_empty_array[2] = {};
__attribute__((used, weak)) int weak_zero_array[2] = {0};

struct S { int x; int y; };
__attribute__((used, weak)) struct S weak_empty_record = {};
__attribute__((used, weak)) struct S weak_zero_record = {0};

__attribute__((used)) int nested_scalar_array[2][2] = {{1,2},{3,4}};
__attribute__((used)) int zero_nested_scalar_array[2][2];
__attribute__((used)) struct S nested_records[2][2] =
    {{{1,2},{3,4}},{{5,6},{7,8}}};
