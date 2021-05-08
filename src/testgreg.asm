# Overflow test case for add
.set noreorder
addi $s0, $zero, 0x01
addi $s1, $zero, 0x7fffffff
nop
nop
add $s2, $s0, $s1
nop
nop
sw $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
