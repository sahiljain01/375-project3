# bgtz --> do not first branch, take second one
# $t5 will be 0x69 if things are right
.set noreorder
    lw $t1, next+4
    lw $t2, next+8
    bgtz $t2, afterFirst
    addi $t0, $zero, 0x44
afterFirst:
    bgtz $t1, afterSecond
    nop
next:
    .word 0xfeedfeed
    .word 0x0f000000
    .word 0xf0000000
afterSecond:
    addi $t5, $zero, 0x69
    .word 0xfeedfeed