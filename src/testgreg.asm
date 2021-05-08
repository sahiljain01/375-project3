# Overflow test case for invalid instr
.set noreorder
lw $t0, 0x18($zero);
nop
nop
addi $s1, $s0, 0x1
next:
.word 0xfeedfeed
value:
.word 0x7fffffff
