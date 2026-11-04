/* A layout-free forward enum is safe in a declaration, not a by-value
 * call result. Neither Core nor the target may invent its return ABI. */
enum unknown_result;
extern enum unknown_result callback(void);
int illegal_call(void) { return (int)callback(); }
