# Overflow test case for invalid instr
.set noreorder
srlv $s0, $s1, $s2
nop
nop
next:
.word 0xfeedfeed
.word 0x7fffffff
