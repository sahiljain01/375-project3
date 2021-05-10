# A simple test case for add immediate unsigned
.set noreorder
ori $s2, $zero, 0x43
addiu $s1, $s2, 0xFFFF
sw $s1, next+4
next:
.word 0xfeedfeed
.word 0x11111111
