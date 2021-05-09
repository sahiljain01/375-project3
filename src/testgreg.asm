# Overflow test case for invalid instr
    .set noreorder
    lw $t0, next+4
    nop
    nop
    lw $t2, next+8
    nop
    nop
    addi $t0, $zero, 0x1
    add $t1, $t0, $t0
    add $t3, $t2, $t0
next:
    .word 0xfeedfeed
    .word 0x22222222
    .word 0x11111111
