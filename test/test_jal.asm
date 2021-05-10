# jal --> $t2 should be the same as the value in $ra
.set noreorder
    addi $t0, $zero, 0x24
    addi $t1, $zero, 0x28
    addi $t2, $zero, ra
    jal next
    .word 0xfeedfeed
ra:
    .word 0xfeedfeed
    .word 0xfeedfeed
    .word 0xfeedfeed
next:
    addi $t3, $zero, 0x32
    .word 0xfeedfeed
