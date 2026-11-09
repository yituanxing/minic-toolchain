#include "target/loongarch64/asm_contract.h"

#include <stdio.h>
#include <string.h>

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

/* The Linux uaccess template is unusually long because it embeds its
 * hardware-register exception-fixup mapping. The entire instruction and
 * relocation table, not just the first ld/st mnemonic, is the machine form. */
#define LOONG_GPR_MAP \
    ".equ .L__gpr_num_zero, 0\n" \
    ".irp num, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n" \
    ".equ .L__gpr_num_$r\\num, \\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_$ra, 1\n" \
    ".equ .L__gpr_num_$tp, 2\n" \
    ".equ .L__gpr_num_$sp, 3\n" \
    ".irp num, 0,1,2,3,4,5,6,7\n" \
    ".equ .L__gpr_num_$a\\num, 4+\\num\n" \
    ".endr\n" \
    ".irp num, 0,1,2,3,4,5,6,7,8\n" \
    ".equ .L__gpr_num_$t\\num, 12+\\num\n" \
    ".endr\n" \
    ".equ .L__gpr_num_$s9, 22\n" \
    ".equ .L__gpr_num_$fp, 22\n" \
    ".irp num, 0,1,2,3,4,5,6,7,8\n" \
    ".equ .L__gpr_num_$s\\num, 23+\\num\n" \
    ".endr\n"

static const char uaccess_load[] =
    "1: ld.b %1, %2\n2:\n" LOONG_GPR_MAP
    ".pushsection __ex_table, \"a\"\n"
    ".balign 4\n"
    ".long ((1b) - .)\n"
    ".long ((2b) - .)\n"
    ".short (2)\n"
    ".short ((((.L__gpr_num_%0) << 0) | ((.L__gpr_num_%1) << 5)))\n"
    ".popsection\n";

static const char sub_positive[] =
    "1: ll.w %1, %2 # atomic_sub_if_positive\n"
    "addi.w %0, %1, %3\n"
    "move %1, %0\n"
    "bltz %0, 2f\n"
    "sc.w %1, %2\n"
    "beqz %1, 1b\n"
    "2: dbar 0x700";

static bool matches(uint32_t id, uint32_t variant, const char *text) {
    return minic_loongarch64_typed_scalar_form_matches_template(
        id, variant, text, strlen(text));
}

static int run(void) {
    static const struct { uint32_t id, variant; const char *source, *normalized; }
    valid[] = {
        {MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR, 2U,
         "dbar 0x700", "dbar 0x700"},
        {MINIC_LOONG_MACHINE_ASM_HINT_DBAR, 1U,
         "dbar %0", "dbar %0"},
        {MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE, 1U,
         sub_positive, sub_positive},
        {MINIC_LOONG_MACHINE_ASM_UACCESS, 1U,
         uaccess_load, uaccess_load},
        {MINIC_LOONG_MACHINE_ASM_PERCPU_AMO, 1U,
         "amadd.w %[ret], %[val], %[ptr]", "amadd.w %0, %2, %1"},
        {MINIC_LOONG_MACHINE_ASM_CSR, 1U,
         "csrrd %[val], %[reg]", "csrrd %0, %1"},
        {MINIC_LOONG_MACHINE_ASM_AMO_STORE, 8U,
         "amadd_db.d $zero, %1, %0", "amadd_db.d $zero, %1, %0"},
        {MINIC_LOONG_MACHINE_ASM_AMO_FETCH, 8U,
         "amadd_db.d %1, %2, %0", "amadd_db.d %1, %2, %0"}
    };
    static const char *const forged[] = {
        "dbar 0x701",                     /* altered privileged barrier */
        "dbar %1",                       /* changed immediate input */
        "1: ll.w %1, %2 # atomic_sub_if_positive\n"
        "addi.w %0, %1, %3\nmove %1, %0\nbltz %0, 2f\n"
        "sc.w %1, %3\nbeqz %1, 1b\n2: dbar 0x700",
        "1: ld.b %2, %1\n2:\n",       /* forged fault register order */
        "amadd.w %0, %1, %2",           /* swapped val and pointer */
        "csrrd %0, 1",                 /* privileged CSR index */
        "amadd_db.d $zero, %0, %1",    /* swapped memory and input */
        "amadd_db.d %1, %0, %2"        /* swapped memory and input */
    };
    char altered_uaccess[sizeof(uaccess_load)];
    char *relocation;
    size_t i;
    for (i = 0U; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        REQUIRE(matches(valid[i].id, valid[i].variant, valid[i].source));
        REQUIRE(matches(valid[i].id, valid[i].variant, valid[i].normalized));
        REQUIRE(!matches(valid[i].id, valid[i].variant + 1U,
                         valid[i].normalized));
        REQUIRE(!matches(valid[i].id, valid[i].variant, forged[i]));
        REQUIRE(!matches(0U, valid[i].variant, valid[i].normalized));
    }
    memcpy(altered_uaccess, uaccess_load, sizeof(altered_uaccess));
    relocation = strstr(altered_uaccess, ".L__gpr_num_%1");
    REQUIRE(relocation != NULL);
    relocation[sizeof(".L__gpr_num_%") - 1U] = '0';
    REQUIRE(!matches(MINIC_LOONG_MACHINE_ASM_UACCESS, 1U,
                     altered_uaccess));
    REQUIRE(!matches(MINIC_LOONG_MACHINE_ASM_PERCPU_AMO, 1U,
                     "amadd.w %0, %2, %1; nop"));
    REQUIRE(!matches(MINIC_LOONG_MACHINE_ASM_CSR, 2U,
                     "csrrd %0, %1"));
    return 0;
}

int main(void) {
    const int failed = run();
    if (failed != 0) {
        (void)fprintf(stderr,
            "LOONG_SCALAR_NORMALIZED_SEAL failed line=%d\n", failed);
        return 1;
    }
    (void)puts("LOONG_SCALAR_NORMALIZED_SEAL=PASS families=8 positive=16"
               " forged_operands=8 forged_variants=8 forged_fixup=1"
               " hardware_privilege=checked");
    return 0;
}
