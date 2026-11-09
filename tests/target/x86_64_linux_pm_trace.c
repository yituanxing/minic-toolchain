/* Original drivers/base/power/main.c pm_trace exact source-qualified GNU asm. */
extern void generate_pm_trace(const void *, int);
#if defined(MINIC_PM_TRACE_NEG_TEMPLATE)
#define MINIC_PM_TRACE_SECTION ".section .badtracedata,\"a\"\n"
#else
#define MINIC_PM_TRACE_SECTION ".section .tracedata,\"a\"\n"
#endif
#if defined(MINIC_PM_TRACE_NEG_LINE_CONSTRAINT)
#define MINIC_PM_TRACE_LINE_CONSTRAINT "r"
#else
#define MINIC_PM_TRACE_LINE_CONSTRAINT "i"
#endif
#if defined(MINIC_PM_TRACE_NEG_LINE_RANGE)
#define MINIC_PM_TRACE_LINE 65536
#else
#define MINIC_PM_TRACE_LINE 598
#endif
#if defined(MINIC_PM_TRACE_NEG_RUNTIME_FILENAME)
extern const char *linux_pm_trace_runtime_filename;
#define MINIC_PM_TRACE_FILENAME linux_pm_trace_runtime_filename
#else
#define MINIC_PM_TRACE_FILENAME "drivers/base/power/main.c"
#endif
#if defined(MINIC_PM_TRACE_NEG_MEMORY_CLOBBER)
#define MINIC_PM_TRACE_CLOBBER : "memory"
#else
#define MINIC_PM_TRACE_CLOBBER
#endif

void linux_pm_trace_resume(void) {
    const void *tracedata;
    asm volatile("movq $1f,%0\n"
                 MINIC_PM_TRACE_SECTION
                 "1:\t.word %c1\n\t"
                 ".quad %c2\n"
                 ".previous"
                 : "=r"(tracedata)
                 : MINIC_PM_TRACE_LINE_CONSTRAINT(MINIC_PM_TRACE_LINE),
                   "i"(MINIC_PM_TRACE_FILENAME)
                 MINIC_PM_TRACE_CLOBBER);
    generate_pm_trace(tracedata, 0);
}
