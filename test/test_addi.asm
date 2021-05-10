# A simple test case for or immediate
.set noreorder
lui $s0, 32767
ori $s1, $s0, 65535
addi $s2, $s1, 1
sw $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
