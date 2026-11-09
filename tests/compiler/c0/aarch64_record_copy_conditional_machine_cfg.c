/* A long record copy emits a real out-of-line MachineCFG loop. The source
 * conditional immediately following it must branch through a physically
 * adjacent false auxiliary, even though the initial Core false block was
 * reserved before the copy loop was introduced. */
struct Record80 {
    unsigned long payload[10];
};

extern struct Record80 source_record;
extern struct Record80 destination_record;
extern int branch_result(int);

int aarch64_record_copy_then_branch(int condition) {
    destination_record = source_record;
    if (condition)
        return branch_result(17);
    return branch_result(23);
}
