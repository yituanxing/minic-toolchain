/* Linux drivers/clk/qcom: F() expands to (2 * (_div) - 1), and the fractional
 * divisor gives the expression type double before conversion to an integer
 * freq_tbl member. This is a valid static arithmetic constant expression. */
enum ClockParent { P_XO, P_GPLL0, P_UNIPHY_RX };
struct FreqTable {
    unsigned long rate;
    unsigned int parent;
    unsigned int hid_div;
    int signed_step;
    unsigned int m;
    unsigned int n;
};
static const struct FreqTable qcom_freqs[] = {
    { (2500000), (P_UNIPHY_RX), (2 * (12.5) - 1), (-2.5), (0), (0) },
    { (171428571), (P_GPLL0), (2 * (3.5) - 1), (0), (0), (0) },
    { (480000000), (P_GPLL0), (2 * (2.5) - 1), (0), (0), (0) },
    { (24000000), (P_XO), (2 * (1) - 1), (0), (0), (0) },
};
int main(void) {
    return qcom_freqs[0].hid_div == 24 && qcom_freqs[0].signed_step == -2 &&
           qcom_freqs[1].hid_div == 6 && qcom_freqs[2].hid_div == 4 &&
           qcom_freqs[3].hid_div == 1 ? 0 : 1;
}
