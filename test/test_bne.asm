# bne --> do not set $t2, do set $t3
.set noreorder
    addi $t0, $zero, 0x24
    addi $t1, $zero, 0x20
    bne $t1, $t0, afterFirst
    nop
    addi $t2, $zero, 0xF0F0
    .word 0xfeedfeed
afterFirst:
    addi $t3, $zero, 0x300
    .word 0xfeedfeed