# Overflow test case for add
.set noreorder
addi $s0, $zero, 0x01
nop
nop
addi $s1, $s0, 0x7fff
nop
nop
sw $s1, next+4
next:
.word 0xfeedfeed
.word 0x11111111
