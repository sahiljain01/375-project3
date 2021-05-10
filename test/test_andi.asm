# A simple test case for or immediate
.set noreorder
lui $s0, 0x44
ori $s1, $s0, 0x44
andi $s2, $s1, 0x00440044
sw $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
