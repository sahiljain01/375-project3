# beq --> take first branch, do not take second one
.set noreorder
    addi $t0, $zero, 0x24
    lw $t1, 0($t4)
    beq $t1, $t0, afterFirst
    addi $t2, $t0, 0x44;
afterFirst:
    addi $t3, $zero, 300
    beq $t2, $t3, afterSecond
    nop
next:
    .word 0xfeedfeed
    .word 0x24
afterSecond:
    addi $t5, $zero, 0x69
    .word 0xfeedfeed
