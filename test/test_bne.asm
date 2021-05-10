# bne --> do not take first branch, do take second one (nop inserted bc branch delay slot always executed)
.set noreorder
    addi $t0, $zero, 0x24
    lw $t1, 0($t4)
    bne $t1, $t0, afterFirst
    nop
    addi $t2, $t0, 0x44
afterFirst:
    addi $t3, $zero, 0x300
    bne $t2, $t3, afterSecond
    nop
next:
    .word 0xfeedfeed
    .word 0x24
afterSecond:
    addi $t5, $zero, 0x69
    .word 0xfeedfeed
