# Overflow test case for invalid instr
    .set noreorder
    lw $t1, next+4
    nop
    nop
    addi $t0, 0x15
    sub $t1, $t1, $t0
    slt $t2, $t1, $t0

    
next:
    .word 0xfeedfeed
    .word 0x00000016
    .word 0x0000008
