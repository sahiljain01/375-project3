# Overflow test case for add
.set noreorder
lw $s0, next+8($zero)
addi $s1, $zero, $0x1
nop
nop
add $s2, $s0, $s1
sw $s1, next+4
next:
.word 0xfeedfeed
.word 0x7fffffff
