typedef _Bool bool;

bool early_boot_irqs_disabled;

enum system_states {
    SYSTEM_BOOTING = 0,
    SYSTEM_RUNNING = 1,
};

enum system_states system_state;

void (*late_time_init)(void);

extern char __attribute__((__section__(".init.data"))) boot_command_line[];
char __attribute__((__section__(".init.data"))) boot_command_line[1024];
char *saved_command_line __attribute__((__section__(".data..ro_after_init")));

int repeated_tentative;
int repeated_tentative;

extern int extern_then_tentative;
int extern_then_tentative;

int tentative_then_extern;
extern int tentative_then_extern;

int tentative_then_full;
int tentative_then_full = 7;

int full_then_tentative = 9;
int full_then_tentative;

struct tentative_record {
    int first;
    long second;
};

struct tentative_record record_state;

extern unsigned long composite_page_table[];
unsigned long composite_page_table[4] __attribute__((__section__(".bss..page_aligned")))
__attribute__((__aligned__(4096)));

extern void *const composite_call_table[];
void *const composite_call_table[4] = {0, 0, 0, 0};
