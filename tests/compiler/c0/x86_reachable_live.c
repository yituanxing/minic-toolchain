extern void visible_side_effect(int);
extern int observable_condition(void);

int runtime_guard(int value) {
    if (value > 0)
        visible_side_effect(value);
    return value;
}

int effectful_condition(int value) {
    if (observable_condition())
        visible_side_effect(value);
    return value;
}

int entry_goto_dead_source_arm(int value) {
    if (0) {
entry_label:
        visible_side_effect(value);
        return value;
    }
    if (value == 42)
        goto entry_label;
    return value;
}
