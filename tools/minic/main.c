#include "driver_internal.h"

int main(int argc, char **argv) {
    MinicDriverInvocation invocation;
    int parsed = minic_driver_cli_parse(argc, argv, &invocation);

    if (parsed == 1)
        return 0;
    if (parsed != 0)
        return parsed;
    return minic_driver_execute(argv[0], &invocation);
}
