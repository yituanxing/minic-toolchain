# Inline fault owner fast-probe retirement

`.github/workflows/linux-runtime-inline-fault-owner-v0.yml` is the earlier fast owner probe for the historical `__minic_inline_spec_720_39` runtime fault.

Later the same day, `linux-runtime-inline-owner-v0.yml` replaced that probe with a complete object scan: it checks every object with `nm`, records every matching owner, and preserves full nm/objdump evidence for the same symbol.

The earlier binary-grep fast probe is therefore archived byte-for-byte under `.github/workflows-disabled/`. The later complete owner scan remains active.
