# Overflow test case for invalid instr
    set noreorder
    lw $t0, next+4
    nop
    nop
    addi $s1, $t0, 0x1
next:
    .word 0xfeedfeed
    .word 0x7fffffff
