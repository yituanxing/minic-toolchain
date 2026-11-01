/* A user section name must not escape into a second GAS statement. */
int __attribute__((used, section(".data.rv64.plan; ebreak")))
    rv64_plan_bad_section = 1;
