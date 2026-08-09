static const char shared_event[] = "shared";

int read_static_pointer_table(void) {
    static const char *const events[] = {
        "one",
        shared_event,
        0,
        "four"
    };
    return events[0] != 0 && events[1] != 0 && events[2] == 0 && events[3] != 0;
}
