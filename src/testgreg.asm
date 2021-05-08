# Overflow test case for invalid instr
.set noreorder
ori $t0, $zero, 0x44
ori $t1, $zero, 0x32
srlv $s0, $s1, $s2
next:
.word 0xfeedfeed
.word 0x7fffffff
