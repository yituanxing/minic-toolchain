/* Linux ixgbe: multiple declarations of a parameter pointer to a fixed
 * array may materialize distinct array descriptor IDs in the frontend.
 * Compatibility is by element type and array shape, not descriptor ID. */
typedef unsigned int u32;
typedef int s32;

struct ixgbe_hw { u32 tag; };
extern s32 ixgbe_fw_phy_activity(struct ixgbe_hw *hw, u32 activity,
                                u32 (*data)[4]);
extern s32 ixgbe_fw_phy_activity(struct ixgbe_hw *hw, u32 activity,
                                u32 (*data)[4]);
s32 ixgbe_fw_phy_activity(struct ixgbe_hw *hw, u32 activity,
                          u32 (*data)[4]) {
    (void)hw;
    (void)data;
    return (s32)(activity + 15U);
}

/* The incomplete bound is compatible with the concrete bound. */
extern s32 compatible_unknown_bound(u32 (*data)[]);
s32 compatible_unknown_bound(u32 (*data)[4]) { (void)data; return 3; }

/* Nested fixed arrays compare all dimensions recursively. */
extern s32 compatible_nested_array(u32 (*data)[2][4]);
s32 compatible_nested_array(u32 (*data)[2][4]) {
    (void)data;
    return 8;
}

int main(void) {
    struct ixgbe_hw hw = { 10U };
    u32 data[4] = { 1U, 2U, 3U, 4U };
    u32 nested[2][4] = {{ 1U, 2U, 3U, 4U }, { 5U, 6U, 7U, 8U }};
    return ixgbe_fw_phy_activity(&hw, 20U, &data) == 35 &&
                   compatible_unknown_bound(&data) == 3 &&
                   compatible_nested_array(&nested) == 8
               ? 0 : 1;
}
