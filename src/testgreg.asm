# Overflow test case for invalid instr
    .set noreorder
    addi $t0, $zero, 0x1
    add $t1, $t0, $t0

next:
    .word 0xfeedfeed
    .word 0x7fffffff
