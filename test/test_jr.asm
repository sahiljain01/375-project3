# jr --> skip 0xfeedfeed
.set noreorder
    addi $t0, $zero, 0x24
    addi $t1, $zero, 0x28
    addi $t2, $zero, next
    jr $t2
    nop
    .word 0xfeedfeed
    .word 0xfeedfeed
    .word 0xfeedfeed
    .word 0xfeedfeed
next:
    addi $t3, $zero, 0x32
    .word 0xfeedfeed