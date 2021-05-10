# jal --> $t2 should hold address of instruction two ahead of jal (ra tag)
# and should be equal to the value at $ra
.set noreorder
    addi $t0, $zero, 0x24
    addi $t1, $zero, 0x28
    addi $t2, $zero, ra
    jal next
    nop
ra:
    .word 0xfeedfeed
    .word 0xfeedfeed
    .word 0xfeedfeed
    .word 0xfeedfeed
next:
    addi $t3, $zero, 0x32
    .word 0xfeedfeed