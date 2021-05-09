# Overflow test case for invalid instr
    .set noreorder
    lw $t1, next+4
    nop
    nop
    jr $t1
    addi $t1, $t1, 0x2
next:
    .word 0xfeedfeed
    .word 0x00000020
afterbranch:
    addi $t2, $zero, 0x99
    .word 0xfeedfeed
