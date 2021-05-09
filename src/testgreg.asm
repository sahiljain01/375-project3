# Overflow test case for invalid instr
    .set noreorder
    lw $t1, next+4
    nop
    nop
    bgtz $t1, $zero, afterbranch
    addi $t3, $zero, 0x44
next:
    .word 0xfeedfeed
    .word 0x00000016
afterbranch:
    addi $t2, $zero, 0x99
    .word 0xfeedfeed
