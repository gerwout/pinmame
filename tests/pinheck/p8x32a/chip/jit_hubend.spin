PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 6 writes a count at every one of its hub slots in a translated loop past the run's end: its slot at cycle
' 399,999 (the end, and every 4096 * k - 1) is taken by a write whose hub cycle comes after it, which must not show
' in the dump. The count at $6000
entry   mov     y, wk
        shl     y, #2
        or      y, #6
        coginit y
        cogid   y
        cogstop y
y       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  wrlong  c, a
        add     c, #1
        jmp     #worker
a       long    $6000
c       long    0
