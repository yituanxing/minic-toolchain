/* Reading a pointer-valued global is not a GNU integer constant expression. */
static int *minic_runtime_pointer;
static const int minic_illegal_static_condition =
    minic_runtime_pointer ? 1 : 0;

int minic_bad_conditional(void) { return minic_illegal_static_condition; }
