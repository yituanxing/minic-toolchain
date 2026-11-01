/* MC-005: GNU aligned sizeof(enum {...}) grows the original enum-constant
 * binding arena. A shallow Parser semantic probe used to steal its owner. */
enum PriorEnum {
    C00 = 0,
    C01 = 1,
    C02 = 2,
    C03 = 3,
    C04 = 4,
    C05 = 5,
    C06 = 6,
    C07 = 7,
    C08 = 8,
    C09 = 9,
    C10 = 10,
    C11 = 11,
    C12 = 12,
    C13 = 13,
    C14 = 14,
#ifndef MC005_SKIP_C15
    C15 = 15,
#endif
#ifdef MC005_ADD_C16
    C16 = 16,
#endif
};

int aligned_enum __attribute__((aligned(sizeof(enum AttributeProbeEnum {
    AttributeProbeConstant = 4
})))) = 2;

enum AttributeProbeEnum visible_enum;

int main(void) {
    visible_enum = AttributeProbeConstant;
    return C00 == 0 && C14 == 14 && visible_enum == 4 && aligned_enum == 2
               ? 0
               : 32;
}
