/* Linux net/rfkill/input.c: module_param named enum stored through
 * unsigned int* type checker. GNU C diagnoses but accepts the mismatch
 * because this completed, nonnegative enum uses unsigned int storage. */
enum master_mode { MASTER_UNLOCK, MASTER_RESTORE, MASTER_UNBLOCK, MASTER_COUNT };
static enum master_mode current_mode = MASTER_RESTORE;

unsigned int *minic_master_mode_address(void) {
    return &current_mode;
}

static unsigned int minic_read_master_mode(unsigned int *value) {
    return *value;
}

unsigned int minic_master_mode_read(void) {
    unsigned int *where = &current_mode;
    return minic_read_master_mode(where);
}

int main(void) {
    return (*minic_master_mode_address() == MASTER_RESTORE &&
            minic_master_mode_read() == MASTER_RESTORE) ? 0 : 1;
}
