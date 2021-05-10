# A simple test case for or immediate
.set noreorder
ori $s0, $zero, 0x234
ori $s1, $zero, 0x11
nop
nop
nor $s2, $s0, $s1
nop
nop
sw $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
