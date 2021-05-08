# A simple test case for and
.set noreorder
addi $s0, $zero, 0x01
addi $s1, $zero, 0x02
add $s2, $s1, $s0
sw $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
