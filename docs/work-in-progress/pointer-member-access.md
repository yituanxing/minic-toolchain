# Pointer member access integration

This short integration is driven by the first pinned tiny-AES expression:

```c
KeyExpansion(ctx->RoundKey, key);
```

Included:

- `->` tokenization;
- typedef-backed record field declarations;
- pointer-to-record member lookup;
- scalar member lvalues;
- array member decay to an element pointer;
- const propagation from the pointed-to record;
- RV64 field-address calculation using the existing record layout;
- focused positive and negative tests;
- GCC/MiniC differential execution and exact frontier advancement.

Deferred:

- direct `.` member access;
- anonymous records or fields;
- bit-fields;
- unions;
- complete byte-width `uint8_t` semantics;
- unrelated compound assignment or control-flow syntax.

Acceptance requires all host configurations, Sanitizers, focused RV64/QEMU tests, the complete differential program matrix, and the pinned tiny-AES frontier gate to pass from a clean checkout.
