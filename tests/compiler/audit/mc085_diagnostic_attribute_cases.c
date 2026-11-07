/* MC-085: GNU compile-time call attributes, one selected TU per case.
 * Preprocess with -DMC085_CASE=N before passing the TU to MiniC. */
#ifndef MC085_CASE
#define MC085_CASE 1
#endif

#if MC085_CASE == 1
extern void stop(void) __attribute__((error("MC085 live error")));
int main(void) { stop(); return 0; }
#elif MC085_CASE == 2
extern void stop(void) __attribute__((error("MC085 dead error")));
int main(void) { if (0) stop(); return 0; }
#elif MC085_CASE == 3
extern void stop(void) __attribute__((error("MC085 constant error")));
int main(void) { if ((1 + 0) == 0) stop(); return 0; }
#elif MC085_CASE == 4
extern void caution(void) __attribute__((warning("MC085 live warning")));
int main(void) { caution(); return 0; }
#elif MC085_CASE == 5
extern void stop(void) __attribute__((error("MC085 indirect error")));
int main(void) { void (*fp)(void) = stop; fp(); return 0; }
#elif MC085_CASE == 6
extern void stop(void) __attribute__((error("MC085 indirect error")));
int call_through(void (*fp)(void)) { fp(); return 0; }
int main(void) { return call_through(stop); }
#elif MC085_CASE == 7
extern void stop(void) __asm__("mc085_alias")
    __attribute__((error("MC085 alias error")));
int main(void) { stop(); return 0; }
#elif MC085_CASE == 8
extern void stop(void) __attribute__((error("MC085 inline guard error")));
static inline __attribute__((always_inline)) void guard(int value) {
    if (value) stop();
}
int main(void) { guard(0); return 0; }
#elif MC085_CASE == 9
extern void stop(void) __attribute__((error("MC085 inactive static error")));
static void dormant(void) { stop(); }
int main(void) { return 0; }
#elif MC085_CASE == 10
extern void stop(void) __attribute__((error("MC085 reachable static error")));
static void dormant(void) { stop(); }
int main(void) { dormant(); return 0; }
#elif MC085_CASE == 11
extern void caution(void) __attribute__((warning("MC085 warning and error"),
                                         error("MC085 warning and error")));
int main(void) { caution(); return 0; }
#elif MC085_CASE == 12
extern void stop(void) __attribute__((error("MC085 conditional error")));
int maybe(int value) { if (value) stop(); return value; }
int main(void) { return maybe(0); }
#elif MC085_CASE == 13
/* Distinct C identifiers deliberately share one linker symbol. Matching
 * Core CALL names to *all* attributed declarations would be a false error. */
extern void dangerous(void) __asm__("mc085_same")
    __attribute__((error("MC085 false alias diagnostic")));
extern void safe(void) __asm__("mc085_same");
int main(void) { safe(); return 0; }
#elif MC085_CASE == 14
extern void dangerous(void) __asm__("mc085_same")
    __attribute__((error("MC085 true alias diagnostic")));
extern void safe(void) __asm__("mc085_same");
int main(void) { dangerous(); return 0; }
#elif MC085_CASE == 15
extern void stop(void) __attribute__((error("MC085 false cold-label error")));
int main(void) { if (0) { cold_label: stop(); } return 0; }
#elif MC085_CASE == 16
extern void stop(void) __attribute__((error("MC085 jumped-over label error")));
int main(void) { goto after; cold_label: stop(); after: return 0; }
#else
#error Unknown MC085 case
#endif
