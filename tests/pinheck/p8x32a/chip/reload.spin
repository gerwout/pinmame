' a cog restarted with other code at the same addresses: translated blocks of the old code must not run
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        mov     z, pa
        shl     z, #16
        or      x, z
        or      x, #%1000
        coginit x wr
        mov     t, cnt
        add     t, dl
        waitcnt t, #0
        mov     y, wk
        add     y, #64
        shl     y, #2
        mov     z, pb
        shl     z, #16
        or      y, z
        or      y, x
        coginit y
        mov     t, cnt
        add     t, dl
        waitcnt t, #0
        cogid   t
        cogstop t
x       long    0
y       long    0
z       long    0
t       long    0
dl      long    30000
pa      long    $6000
pb      long    $6100
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
workerA mov     n, #200
        mov     acc, #0
:l      add     acc, #1
        add     acc, #3
        xor     acc, n
        djnz    n, #:l
        wrlong  acc, par
        cogid   n
        cogstop n
n       long    0
acc     long    0
        long    0
        long    0
        long    0
        long    0
        long    0
        org     0
workerB mov     nb, #200
        mov     accb, #0
:lb      add     accb, #1
        sub     accb, #3
        xor     accb, nb
        djnz    nb, #:lb
        wrlong  accb, par
        cogid   nb
        cogstop nb
nb      long    0
accb     long    0
        long    0
        long    0
        long    0
        long    0
        long    0
