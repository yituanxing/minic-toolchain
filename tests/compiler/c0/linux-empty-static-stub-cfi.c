/* Linux CONFIG-disabled CFI stubs may take linker-script symbol addresses,
 * but an empty internal helper with pure arguments must not keep relocations
 * to those otherwise undefined symbols alive. This need not be `inline`. */
extern char minic_missing_cfi_start;
extern char minic_missing_cfi_end;
extern int __cfi_sites[], __cfi_sites_end[];
extern int __retpoline_sites[], __retpoline_sites_end[];

static void cfi_disabled_stub(void *start, void *end)
{
}

static void cfi_side_effect_stub(int value)
{
}

/* Same non-inline CONFIG_FINEIBT=n source shape as Linux v6.6.143's
 * __apply_fineibt() from arch/x86/kernel/alternative.c. Here the externs
 * are arrays, so C uses implicit array-to-pointer conversion. */
static void fineibt_disabled_stub(int *ret_start, int *ret_end,
                                  int *cfi_start, int *cfi_end, _Bool builtin)
{
}

static volatile int cfi_observed;

int main(void)
{
    cfi_disabled_stub(&minic_missing_cfi_start, &minic_missing_cfi_end);
    fineibt_disabled_stub(__retpoline_sites, __retpoline_sites_end,
                          __cfi_sites, __cfi_sites_end, 1);
    cfi_side_effect_stub(++cfi_observed);
    return cfi_observed == 1 ? 0 : 1;
}
