# Historical hrtimer and tick-sched owner differentials

The historical `linux-runtime-hrtimer-owner-v0.yml` and `linux-runtime-tick-sched-owner-v0.yml` workflows were single-owner before/after diagnostics.

The later `linux-runtime-timer-focused-v0.yml` refreshes the same cumulative owner chain through timer, hrtimer and tick-sched, retains the current MiniC assembly plus complete nm/objdump evidence for hrtimer and tick-sched, extracts their focused functions, relinks the early image, and runs the current QEMU frontier before continuing through RCU, init/main and softirq.

The two older differential YAML files are archived byte-for-byte under `.github/workflows-disabled/`. Their exact before/after experiments remain recoverable from history.

`linux-runtime-rcu-owner-v0.yml` remains active because it additionally exercises the rest-init frontier and kallsyms before/after evidence, which is not covered by the timer-focused workflow.
