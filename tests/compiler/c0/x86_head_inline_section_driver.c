unsigned long probe_early(void);
unsigned long probe_later(void);
unsigned long probe_early_again(void);
unsigned long probe_explicit_owner(void);
unsigned long probe_nested_early(void);
unsigned long probe_nested_later(void);
unsigned long probe_overflow_early(void);
unsigned long probe_overflow_later(void);

int main(void) {
    return probe_early() != 144UL ||
           probe_later() != 144UL ||
           probe_early_again() != 144UL ||
           probe_explicit_owner() != 39UL ||
           probe_nested_early() != 60UL ||
           probe_nested_later() != 60UL ||
           probe_overflow_early() != 6UL ||
           probe_overflow_later() != 6UL;
}
