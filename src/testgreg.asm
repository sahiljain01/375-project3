# Overflow test case for invalid instr
.set noreorder
ll $s0, $s1, 0x44
nop
nop
next:
.word 0xfeedfeed
.word 0x7fffffff
