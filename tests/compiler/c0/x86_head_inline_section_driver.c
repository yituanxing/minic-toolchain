unsigned long probe_early(void);
unsigned long probe_later(void);
unsigned long probe_early_again(void);
unsigned long probe_explicit_owner(void);

int main(void) {
    return probe_early() != 144UL ||
           probe_later() != 144UL ||
           probe_early_again() != 144UL ||
           probe_explicit_owner() != 39UL;
}
