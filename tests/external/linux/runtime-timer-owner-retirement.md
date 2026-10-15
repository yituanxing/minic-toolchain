# Historical timer owner differential retirement

`.github/workflows/linux-runtime-timer-owner-v0.yml` was introduced during the September owner-localization phase as a single-owner before/after experiment around `kernel/time/timer.o`.

The later `linux-runtime-timer-focused-v0.yml` workflow retains the current-MiniC timer owner, its complete nm/objdump evidence, relinks and executes the runtime frontier, and extends the refreshed chain through hrtimer, tick-sched, RCU, init/main and softirq with linked first-fault diagnostics.

The historical before/after workflow is therefore archived byte-for-byte under `.github/workflows-disabled/`. The newer focused timer frontier remains active, and the original differential experiment can still be reconstructed from the archived YAML if needed.
