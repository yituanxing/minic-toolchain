# Linux runtime owner focused V1 contract

\`.github/workflows/linux-runtime-owner-focused-v1.yml\` consolidates the four stable owner-focused runtime frontier workflows into one matrix without collapsing their target-specific evidence.

The matrix contains four ordered owner-chain modes:

- \`timekeeping\`: refresh through \`kernel/time/timekeeping.o\` and retain \`timekeeping_advance\`.
- \`vsyscall\`: refresh through \`kernel/time/vsyscall.o\` and retain \`update_vsyscall\`.
- \`notifier\`: refresh through \`kernel/notifier.o\` and retain \`raw_notifier_call_chain\`.
- \`build-policy\`: refresh through \`kernel/sched/build_policy.o\` and retain \`account_process_tick\`.

Every mode restores the same certified full linked fixture, pinned Linux 6.6.143 source, frozen linker source subset, builds the exact centralized MiniC runtime profile, refreshes the PI owner and the same cumulative frontier-owner chain used by the historical focused workflow, relinks the early image, and runs the same event-driven frontier watcher against \`linux-runtime-init-irq-frontier-v0.json\`.

The historical focused workflows remain active until all four matrix modes are independently green on the cleanup branch. Their older non-focused owner-differential workflows are not covered by this contract because those retain before/after owner evidence and require a separate retirement decision.
