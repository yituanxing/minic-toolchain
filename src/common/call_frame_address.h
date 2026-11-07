#ifndef MINIC_COMMON_CALL_FRAME_ADDRESS_H
#define MINIC_COMMON_CALL_FRAME_ADDRESS_H

/* Source builtin categories and target admission query agree on this small
 * semantic contract. Neither side needs to include the other one's AST. */
typedef enum MinicCallFrameAddressKind {
    MINIC_CALL_FRAME_ADDRESS_RETURN = 0,
    MINIC_CALL_FRAME_ADDRESS_FRAME
} MinicCallFrameAddressKind;

#endif
