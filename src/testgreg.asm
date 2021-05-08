# A simple test case for sub
.set noreorder
addi $s0, $zero, 0x03
addi $s1, $zero, 0x02
nop
nop
sub $s2, $s0, $s1
nop
nop
sw $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
