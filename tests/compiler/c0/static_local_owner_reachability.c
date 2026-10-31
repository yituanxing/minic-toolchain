/* No special section name is needed for reachability: metadata in a dead
 * inline function must not become an independent translation-unit root. */
extern int minic_dead_only_missing_function(void);
extern int minic_dead_call_only_missing_function(void);

int minic_owner_value = 12;

static int dead_reloc_target(void) {
    return minic_dead_call_only_missing_function();
}

static inline int dead_inline(int value) {
    static void * __attribute__((used, section(".data.owner_dead"))) address =
        (void *)&minic_dead_only_missing_function;
    /* Reproduces x86 Linux vDSO __SCK__ relocation ownership independently
     * of the spelling of the discard section. */
    static void * __attribute__((used, section(".discard.addressable"))) discarded =
        (void *)&minic_dead_only_missing_function;
    /* A relocation in dead local storage must not pull in another static
     * function whose body would introduce an undefined call of its own. */
    static void * __attribute__((used, section(".data.owner_dead_target"))) chained =
        (void *)&dead_reloc_target;
    static int counter = 7;
    static const char tag[] = "dead";
    return value + counter + tag[0] + (address != 0) + (discarded != 0) +
           (chained != 0) +
           (__func__[0] == 'd');
}

static inline int live_inline(int value) {
    static void * __attribute__((used, section(".data.owner_live"))) address =
        (void *)&minic_owner_value;
    static int counter = 5;
    static const char tag[] = "ok";
    return value + counter + *(int *)address + (tag[0] == 'o') +
           (__func__[0] == 'l') - 1;
}

/* GNU 'used' preserves this function and its function-local static storage,
 * even though no executable code calls the inline definition. */
static inline __attribute__((used)) int forced_inline(void) {
    static void * __attribute__((used, section(".data.owner_forced"))) address =
        (void *)&minic_owner_value;
    return *(int *)address;
}

static int __attribute__((used, section(".data.owner_top"))) top_level = 3;

int minic_static_owner_result(void) {
    return live_inline(24);
}

int main(void) {
    return minic_static_owner_result() == 42 && top_level == 3 ? 0 : 1;
}
