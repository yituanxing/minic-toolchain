/* Linux drivers/clk/imx/clk-imx93.c has arrays of pointer arrays whose
 * elements are separately brace-enclosed, not scalar pointer initializers. */
static const char *const parent_names[3][4] = {
    {"osc_24m", "sys_pll_pfd0_div2", "sys_pll_pfd1_div2", "video_pll"},
    {"osc_24m", "sys_pll_pfd0_div2", "sys_pll_pfd1_div2", "sys_pll_pfd2_div2"},
    {"osc_24m", "sys_pll_pfd1_div2", "video_pll", "sys_pll_pfd2_div2"}
};

static const char *other_names[2][3] = {
    [0] = {[2] = "zero-two", [0] = "zero-zero"},
    [1] = {[1] = "one-one"}
};

static const char *last_names[2][2] = {
    {"up"}, {"down", "right"}
};

int main(void) {
    return parent_names[0][0][0] != 'o' ||
           parent_names[1][3][8] != 'p' ||
           parent_names[2][2][0] != 'v' ||
           other_names[0][0][5] != 'z' ||
           other_names[0][1] != 0 ||
           other_names[0][2][5] != 't' ||
           other_names[1][1][4] != 'o' ||
           last_names[0][0][0] != 'u' ||
           last_names[0][1] != 0 ||
           last_names[1][1][0] != 'r';
}
