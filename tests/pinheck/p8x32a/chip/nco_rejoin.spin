' a counter leaves the NCO list before its write takes effect (another cog restarts its cog within 2 cycles of the
' write) and rejoins: the level it had must not show before the new write takes effect
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     ctrb, ctrf
        mov     frqb, fast
        or      dira, p5
        mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     n, #24
:loop   mov     t, cnt
        add     t, dl
        waitcnt t, #0
        mov     y, wk
        shl     y, #2
        mov     z, n
        shl     z, #18
        or      y, z
        or      y, x
        coginit y
        djnz    n, #:loop
        mov     t, cnt
        add     t, dl
        waitcnt t, #0
        cogstop x
        cogid   t
        cogstop t
x       long    0
y       long    0
z       long    0
t       long    0
n       long    0
dl      long    9000
p5      long    1 << 5
ctrf    long    %00100 << 26 | 5
fast    long    $8000_0000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%10
        mov     phsa, ph1
        mov     ctra, ctrv
        mov     w, par
        shr     w, #2
        add     w, #40
        add     w, cnt
        waitcnt w, #0
:l      mov     ctra, #0
        mov     ctra, ctrv
        jmp     #:l
ctrv    long    %00100 << 26 | 1
ph1     long    $8000_0000
w       long    0
