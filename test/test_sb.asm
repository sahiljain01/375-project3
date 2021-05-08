# A simple test case for or immediate
.set noreorder
ori $s0, $zero, 0x44
ori $s1, $zero, 0x32
nop
nop
add $s2, $s0, $s1
nop
nop
sb $s2, next+4
next:
.word 0xfeedfeed
.word 0x11111111
