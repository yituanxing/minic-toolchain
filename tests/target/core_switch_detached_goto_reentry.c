/* A goto into a switch can enter a C label immediately before a case label.
 * The source segment before it may itself terminate. The Core switch owner
 * must lower the detached reentry, including its fallthrough to the case,
 * not leave an empty referenced block for the verifier to reject. */
int minic_switch_detached_reentry(int choice, int condition) {
    switch (choice) {
    case 0:
        if (condition) goto custom_auto;
        return 10;
    case 1:
        if (condition) {
            goto custom_auto;
        } else {
            return 11;
        }
        break;
    custom_auto:
    case 2:
        return choice + 20;
    default:
        return 30;
    }
    return 40;
}

int minic_switch_sibling_reentry(int mode) {
    switch (mode) {
    case 0:
        goto alpha;
    case 1:
        return 1;
        break;
    alpha:
    case 2:
        return 2;
    default:
        return 3;
    }
}
