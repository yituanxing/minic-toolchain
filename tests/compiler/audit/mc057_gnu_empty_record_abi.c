/* MC-057: compile with exactly one -DTEST_{RETURN,PARAM,CALL_RETURN,CALL_PARAM}.
 * GNU C empty record argument/return consumes no physical ABI data. */
struct Empty {};
#if defined(TEST_RETURN)
struct Empty ret_empty(void) { return (struct Empty){}; }
#elif defined(TEST_PARAM)
struct Empty pass_empty(struct Empty e) { return e; }
#elif defined(TEST_CALL_RETURN)
struct Empty ret_empty(void);
int call_ret(void) { ret_empty(); return 17; }
#elif defined(TEST_CALL_PARAM)
struct Empty pass_empty(struct Empty e);
int call_pass(void) { struct Empty e = {}; pass_empty(e); return 19; }
#else
#error "Select a GNU empty record ABI probe"
#endif
