# Deliberately places an actual ISA instruction at an odd byte offset.
# GAS is the oracle for whether this is an error or a byte stream: do not
# accept/reject it based on the overall .text section length.
.text
.byte 0
addi.d $r4,$r4,0
