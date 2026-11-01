/* ELF section attributes must be data, not arbitrary assembler statements. */
int __attribute__((used, section(".data.rv32.plan; ebreak"))) rv32_plan_injected = 1;
