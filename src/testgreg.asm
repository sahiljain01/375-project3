# Overflow test case for invalid instr
.set noreorder
lw $s0, value
nop
nop
addi $s1, $s0, 0x1
next:
.word 0xfeedfeed
value:
.word 0x7fffffff
