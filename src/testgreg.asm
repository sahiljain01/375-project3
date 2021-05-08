# Overflow test case for invalid instr
.set noreorder
ll $t4, $zero, 0x04;
nop
nop
next:
.word 0xfeedfeed
.word 0x7fffffff
