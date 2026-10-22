#include "target/loongarch64/scalar_normalize.h"
bool minic_loongarch64_emit_integer_normalize(FILE *out, size_t width, bool sign) {
    const char *op = width == 1U
        ? (sign ? "  slli.w $t0, $t0, 24\n  srai.w $t0, $t0, 24\n" :
                  "  andi $t0, $t0, 255\n")
        : width == 2U
            ? (sign ? "  slli.w $t0, $t0, 16\n  srai.w $t0, $t0, 16\n" :
                      "  bstrpick.w $t0, $t0, 15, 0\n")
            : width == 4U ? "" : NULL;
    return op != NULL && fputs(op, out) != EOF;
}
